/*
 * Credits: Julian Oliver, 2008-2009 <julian@julianoliver.com>.
 * 
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * This code builds upon BazAR, in particular 'multigl.cpp'. It has been
 * modified to support texture and video-texture mapping to an OpenGL plane over
 * the ROI. The ROI in the model image is now read in from a file generated by
 * the training process. Pose estimation stabilisation, augmentation fades,
 * fonts, mouse input hooks, augmentation over archival video and other bits have
 * pieces have been added also.
 *
 * I've fixed a bug in BazAR's planar_object_recognizer::build_with_cache where
 * corner values for the ROI were only being set immediately after training, not
 * on plain init.
 * 
 * Usage:
 * 
 * There are four ways to use Artvertiser.
 * 
 * With video substitution of the ROI: 
 *
 *	 ./artvertiser -m <model file> -a <avi file> 
 *
 * With video substitution of the ROI and capture from an AVI file
 *
 *  ./artvertiser -m <model file> -a <avi file> -b <avi file>
 *
 * With image substitution of the ROI and capture from a v4l device
 *
 * 	./artvertiser -m <model file> 
 *
 * See defines below for setting capture window size and V4L device index
 *
*/

#include <iostream>
#include <sstream> // for conv int->str 
#include <vector>
#include <cv.h>
#include <highgui.h>
#include <map>

#include <stdio.h>
#include <time.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <calib/camera.h>
#include "multigrab.h"

#ifdef HAVE_APPLE_OPENGL_FRAMEWORK
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "/usr/include/freetype2/freetype/config/ftconfig.h"
#include <FTGL/ftgl.h>

#define IsRGB(s) ((s[0] == 'R') && (s[1] == 'G') && (s[2] == 'B'))
#define IsBGR(s) ((s[0] == 'B') && (s[1] == 'G') && (s[2] == 'R'))

#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#define GL_MIRROR_CLAMP_EXT 0x8742

#define WIDTH 320 
#define HEIGHT 240
#define V4LDEVICE 0

#define NUMARTVERTS 5

//#define WIDTH 320
//#define HEIGHT 240 

MultiGrab *multi=0;
CamCalibration *calib=0;
CvPoint projPts[4];
IplTexture *frameTexture=0;
IplTexture *tex=0;
IplImage *image = 0;
CvCapture *capture = 0; 
CvCapture *avi_capture = 0;
IplImage *avi_image = 0;
IplImage *avi_frame = 0;
IplImage *model_image = 0;
IplImage *this_frame = 0;
IplImage *last_frame  = 0;
IplImage *diff= 0;
IplImage *bit_frame= 0;

// load some images. hard-coded for know until i get the path parsing together.
IplImage *image1 = cvLoadImage("artvert1.png");
IplImage *image2 = cvLoadImage("artvert2.png");
IplImage *image3 = cvLoadImage("artvert3.png");
IplImage *image4 = cvLoadImage("artvert4.png");
IplImage *image5 = cvLoadImage("artvert5.png");

// define a container struct for each artvert
struct artvert_struct
{
	const char *artvert;
	IplImage *image;
	const char *date;
	const char *author;
	const char *advert;
	const char *street;
};	

typedef vector<artvert_struct> artverts_list;
artverts_list artverts(5);

// create a vector for the images and initialise it.
typedef vector<IplImage> imgVec;

bool frame_ok=false;
bool cache_light=false;
bool dynamic_light=false;
bool sphere_object=false;
bool avi_play=false;
bool avi_play_init=false;
bool lbutton_down = false;
bool label = false;

double a_proj[3][4];
double old_a_proj[3][4];
float fade = 0.0;
float fade_speed = 2;
int difference = 0;
int have_proj = 0;
int nb_light_measures=0;
int geom_calib_nb_homography;
int current_cam = 0;
int avi_init = 0;
int found_match = 0;
int augment = 0;
int cnt=0;

vector<int> roi_vec;
CvPoint2D32f *c1 = new CvPoint2D32f[4];

char *image_path;
char *model_file = "model.bmp";

static void start();
static void geomCalibStart(bool cache);

// initialise a couple of fonts.
CvFont font, fontbold;

// Gl format for texturing
GLenum format;
GLuint imageID;


// ftgl font setup
static FTFont *ftglFont;

/* use this to read paths from the file system

string getExtension(const string &file)
{
	string::size_type dot = file.rfind('.');
	string lcpath = file;
	string suffix;
	transform(lcpath.begin(), lcpath.end(), lcpath.begin(), tolower);
	if(dot != string::npos) 
	{
		suffix = lcpath.substr(dot + 1);
	}
	return suffix;
}
*/

std::string date(int now)
{
	time_t rawtime;
	struct tm *timeinfo;
	char tBuffer[80];
	time ( &rawtime );
 	timeinfo = localtime ( &rawtime );
	strftime (tBuffer,80,"%I:%M:%S:%p, %d %b %Y",timeinfo);
	string timeStr;
	stringstream _timeStr;
	_timeStr << tBuffer;
	timeStr = _timeStr.str();
	return timeStr;
}


// mouse input using GLUT.
void entry(int state)
{
	if (state == GLUT_ENTERED)
		cout << "Mouse Entered" << endl;
	else
		cout << "Mouse Left" << endl;
}

void mouse(int button, int state, int x, int y)
{
	if (button == GLUT_RIGHT_BUTTON)
	{
		if (state == GLUT_DOWN)
		{
			label = true;
		}
		
		else
		{
			label = false;
		}
	}
	else if (button == GLUT_LEFT_BUTTON)
	{
		if (state == GLUT_DOWN)
		{
			lbutton_down = true;
			if (cnt >= NUMARTVERTS-1)
			{
				cnt = 0;
			}
		  	else
			{
				cnt ++;
			}
		}
		else
			lbutton_down = false;
	}
	cout << "we are on image " << cnt << endl;
}
	
// text drawing function
static void drawText(IplImage *img, const char *text, CvPoint point, CvFont *font, CvScalar colour, double size)
{
	cvInitFont( font, CV_FONT_HERSHEY_DUPLEX, size, size, 0, 1, CV_AA);
	//cvInitFont( font, CV_FONT_HERSHEY_PLAIN, size, size, 0, 1, CV_AA);
	cvPutText(img, text, point, font, colour);
}

// read in ROI coords from txt file into vector.
static vector<int>  readROI(const char *filename)
{
	cout << filename << endl;
	string l;
	ifstream roi(filename);
	vector<int> v;
	int coord;
	char s[10];
	char *s1;
	int lines = 0;

	if (roi.is_open())
	{
		while (!roi.eof())
		{
			getline(roi, l);
			strcpy(s, l.c_str());
			s1 = strtok(s, " ");
	
			while (s1 != NULL)
			{
				//roi_vec.push_back(atoi(s1));
				v.push_back(atoi(s1));
				s1 = strtok(NULL, " ,");
			}	
		}
		roi.close();
	}
	else 
	{
		cout << "roi file not found" << endl;
	}
	return v;
}

//! GLUT callback on window size change 
static void reshape(int width, int height)
{
	//GLfloat h = (GLfloat) height / (GLfloat) width;
    int winWidth  = WIDTH; 
    int winHeight = HEIGHT;
    glViewport(0,0,winWidth, winHeight);
	glutPostRedisplay();
}

//! Print a command line help and exit.
static void usage(const char *s) {
	cerr << "usage:\n" << s
			<< "[-m <model image>] [-r]\n"
			"   -a  specify path to AVI (instead of v4l device)\n"
			"	-b	specify path to AVI video artvert\n"
			"	-m	specifies model image\n"
			"	-r	do not load any data\n"
			"	-t	train a new classifier\n"
			"	-g	recompute geometric calibration\n"
			" 	-a  <path> load an AVI movie as an artvert\n"
			"	-l	rebuild irradiance map from scratch\n";
	exit(1);
}

/*!\brief Initialize everything
 *
 * - Parse the command line
 * - Initialize all the cameras
 * - Load or interactively build a model, with its classifier.
 * - Set the GLUT callbacks for geometric calibration or, if already done, for photometric calibration.
 */

static bool init( int argc, char** argv )
{
	// more from before init should be moved here
	bool redo_geom=false;
	bool redo_training=false;
	bool redo_lighting=false;
	char *avi_bg_path="";

	// parse command line
	for (int i=1; i<argc; i++) {
		if (strcmp(argv[i], "-m") ==0) {
			if (i==argc-1) usage(argv[0]);
			model_file = argv[i+1];
			i++;
		} else if (strcmp(argv[i], "-r")==0) {
			redo_geom=redo_training=redo_lighting=true;
		} else if (strcmp(argv[i], "-g")==0) {
			redo_geom=true;
		} else if (strcmp(argv[i], "-l")==0) {
			redo_lighting=true;
		} else if (strcmp(argv[i], "-t")==0) {
			redo_training=true;
		} else if (strcmp(argv[i], "-a")==0) {
			avi_capture=cvCaptureFromAVI(argv[i+1]);
			avi_play=true;
		} else if (strcmp(argv[i], "-b")==0) {
			avi_bg_path=argv[i+1];
		} else if (strcmp(argv[i], "-i")==0) {
			image_path=argv[i+1];
		} else if (argv[i][0]=='-') {
			usage(argv[0]);
		} 
	}

	cout << avi_bg_path << endl;	
	cache_light = !redo_lighting;

	multi = new MultiGrab(model_file);

	if( multi->init(!redo_training, model_file, avi_bg_path, WIDTH, HEIGHT, V4LDEVICE) ==0)
	{
		cerr <<"Initialization error.\n";
		return false;
	}

	geomCalibStart(!redo_geom);

	artvert_struct artvert1 = {"Arrebato, 1980", image1, "Feb, 2009", "Iván Zulueta", "Polo", "Madrid, Spain"};
	artvert_struct artvert2 = {"name2", image2, "2008", "simon innings", "Helmut Lang", "Parlance Avenue"};
	artvert_struct artvert3 = {"name3", image3, "2008", "simon innings", "Loreal", "Parlance Avenue"};
	artvert_struct artvert4 = {"name4", image4, "2008", "simon innings", "Hugo Boss", "Parlance Avenue"};
	artvert_struct artvert5 = {"name5", image5, "2008", "simon innings", "Burger King", "Parlance Avenue"};

	artverts[0] = artvert1;
	artverts[1] = artvert2;
	artverts[2] = artvert3;
	artverts[3] = artvert4;
	artverts[4] = artvert5;

	// copy char model_file before munging with strcat
    char s[strlen(model_file)];
    strcpy (s, model_file);
    strcat(s, ".roi");
    roi_vec = readROI(s);

	// load model_image for use with diffing, later
	model_image = cvLoadImage(model_file);

	c1[0].x = roi_vec[0];
	c1[0].y = roi_vec[1];
	c1[1].x = roi_vec[2];
	c1[1].y = roi_vec[3];
	c1[2].x = roi_vec[4];
	c1[2].y = roi_vec[5];
	c1[3].x = roi_vec[6];
	c1[3].y = roi_vec[7];

	return true;
}

/*! The keyboard callback: reacts to '+' and '-' to change the viewed cam, 'q' exits.
 * 'd' turns on/off the dynamic lightmap update.
 * 'f' goes fullscreen.
 */
static void keyboard(unsigned char c, int x, int y)
{
	switch (c) {
		case 'n' : if (augment == 1)
						augment = 0;
					else
						augment = 1;
		case '+' : if (current_cam < multi->cams.size()-1) 
				   current_cam++;
			   break;
		case '-': if (current_cam >= 1) 
				  current_cam--;
			  break;
		case 'q': exit(0); break;
		case 'd': dynamic_light = !dynamic_light; break;
		case 'a': if (avi_play == true)
					avi_play = false;
				  else
					avi_play = true;
		case 'f': glutFullScreen(); break;
		case 'i': if (cnt >= NUMARTVERTS-1)
					cnt = 0;
				  else
					cnt ++;
				  cout << "we are on image " << cnt << endl;
				  break;
	}
	glutPostRedisplay();
}

static void emptyWindow() 
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

int main(int argc, char *argv[])
{
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
    glutInitWindowSize(WIDTH,HEIGHT); // hard set the init window size
    //glutInitWindowSize(800,450); // hard set the init window size

	glutDisplayFunc(emptyWindow);
    glutReshapeFunc(reshape);
	glutCreateWindow("The Artvertiser 0.4");
	glutMouseFunc(mouse);
	glutEntryFunc(entry);

    //ftglFont = new FTBufferFont("/usr/share/fonts/truetype/freefont/FreeMono.ttf");
    ftglFont = new FTBufferFont("fonts/FreeSans.ttf");
    ftglFont->FaceSize(12);
    ftglFont->CharMap(ft_encoding_unicode);

	if (!init(argc,argv)) return -1;

	cvDestroyAllWindows();

	glutKeyboardFunc(keyboard);
	glutMainLoop();
	return 0; /* ANSI C requires main to return int. */
}

//!\brief  Draw a frame contained in an IplTexture object on an OpenGL viewport.
static bool drawBackground(IplTexture *tex)
{
	if (!tex || !tex->getIm()) return false;

	IplImage *im = tex->getIm();
	int w = im->width-1;
	int h = im->height-1;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	tex->loadTexture();

	glBegin(GL_QUADS);
	glColor4f(1,1,1,1);

	glTexCoord2f(tex->u(0), tex->v(0));
	glVertex2f(-1, 1);
	glTexCoord2f(tex->u(w), tex->v(0));
	glVertex2f(1, 1);
	glTexCoord2f(tex->u(w), tex->v(h));
	glVertex2f(1, -1);
	glTexCoord2f(tex->u(0), tex->v(h));
	glVertex2f(-1, -1);
	glEnd();

	tex->disableTexture();

	return true;
}

/*! \brief A draw callback during camera calibration
 *
 * GLUT calls that function during camera calibration when repainting the
 * window is required.
 * During geometric calibration, no 3D is known: we just plot 2d points
 * where some feature points have been recognized.
 */
static void geomCalibDraw(void)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glDisable(GL_LIGHTING);
	drawBackground(frameTexture);

	if (!multi) return;

	IplImage *im = multi->cams[current_cam]->frame;
	planar_object_recognizer &detector(multi->cams[current_cam]->detector);
	if (!im) return;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, im->width-1, im->height-1, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_BLEND);
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);

	if (detector.object_is_detected) 
	{
		glPointSize(2);
		glBegin(GL_POINTS);
		glColor4f(0,1,0,1);
		for (int i=0; i<detector.match_number; ++i) 
		{
			image_object_point_match * match = detector.matches+i;
			if (match->inlier) 
			{
				int s = (int)(match->image_point->scale);
				float x=PyrImage::convCoordf(match->image_point->u, s, 0);
				float y=PyrImage::convCoordf(match->image_point->v, s, 0);
				glVertex2f(x,y);
			}
		}
		glEnd();
	}

	glutSwapBuffers();
}

/*!\brief Called when geometric calibration ends. It makes
 * sure that the CamAugmentation object is ready to work.
 */
static void geomCalibEnd()
{

	if (!multi->model.augm.LoadOptimalStructureFromFile("camera_c.txt", "camera_r_t.txt")) 
	{
		cout << "failed to load camera calibration.\n";
		exit(-1);
	}
	glutIdleFunc(0);
	//glutDisplayFunc(0);
	delete calib;
	calib=0;
}

/*! Called by GLUT during geometric calibration when there's nothing else to do.
 * This function grab frames from camera(s), run the 2D detection on every image,
 * and keep the result in memory for calibration. When enough homographies have
 * been detected, it tries to actually calibrate the cameras.
 */
static void geomCalibIdle(void)
{
	// acquire images
	multi->grabFrames();

	// detect the calibration object in every image
	// (this loop could be paralelized)
	int nbdet=0;
	for (int i=0; i<multi->cams.size(); ++i) 
	{
		if (multi->cams[i]->detect()) nbdet++;
	}

	if(!frameTexture) frameTexture = new IplTexture;
	frameTexture->setImage(multi->cams[current_cam]->frame);

	if (nbdet>0) {
		for (int i=0; i<multi->cams.size(); ++i) {
			if (multi->cams[i]->detector.object_is_detected) {
				add_detected_homography(i, multi->cams[i]->detector, *calib);
			} else {
				calib->AddHomography(i);
			}
		}
		geom_calib_nb_homography++;
	}

	if (geom_calib_nb_homography>=150) {
		if (calib->Calibrate(
					50, // max hom
					(multi->cams.size() > 1 ? 1:2),   // padding or random
					(multi->cams.size() > 1 ? 0:3),
					1,   // padding ratio 1/2
					0,
					0,
					0.0078125,	//alpha
					0.9,		//beta
					0.001953125,//gamma
					10,	  // iter
					0.05, //eps
					3   //postfilter eps
				   )) 
		{
			calib->PrintOptimizedResultsToFile1();
			geomCalibEnd();
			start();
			return;
		}
	}
	glutPostRedisplay();
}

/*!\brief Start geometric calibration. If the calibration can be loaded from disk,
 * continue directly with photometric calibration.
 */
static void geomCalibStart(bool cache)
{
	if (cache && multi->model.augm.LoadOptimalStructureFromFile("camera_c.txt", "camera_r_t.txt")) {
		start();
		return;
	}

	// construct a CamCalibration object and register all the cameras
	calib = new CamCalibration();

	for (int i=0; i<multi->cams.size(); ++i) {
		calib->AddCamera(multi->cams[i]->width, multi->cams[i]->height);
	}

	geom_calib_nb_homography=0;
	glutDisplayFunc(geomCalibDraw);
	glutIdleFunc(geomCalibIdle);
}

//#define DEBUG_SHADER
/*! The paint callback during photometric calibration and augmentation. In this
 * case, we have access to 3D data. Thus, we can augment the calibration target
 * with cool stuff.
 */
static void draw(void)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_LIGHTING);

	drawBackground(frameTexture);

	string cnt_str;
	stringstream cnt_out;
	cnt_out << cnt;
	cnt_str = cnt_out.str();

	//IplImage *pre_mask = cvCreateImage(cvSize(WIDTH, HEIGHT), 8, 3); 

	if (!multi) return;

	IplImage *im = multi->model.image;

	if (!im) return;

    int now = glutGet(GLUT_ELAPSED_TIME);
	/* elapsed time
	cout << now/1000.0 << endl;
	*/
	// pixel shift

	if (frame_ok and augment == 1) {

		found_match = 1;

		// here i calculate pixel shift to help us remove pose estimation jitter
		IplImage *_frame = frameTexture->getIm();

		if (!difference)
		{
			this_frame=cvCreateImage(cvSize(WIDTH, HEIGHT),8,1);
			bit_frame=cvCreateImage(cvSize(WIDTH, HEIGHT),8,1);
			last_frame=cvCloneImage(this_frame);
			diff=cvCreateImage(cvSize(WIDTH, HEIGHT), 8 ,1);
			difference=1;
		}

        cvCvtColor(_frame, this_frame,CV_BGR2GRAY);
        cvAbsDiff(this_frame,last_frame,diff);
        //threshold image
        cvThreshold(diff,bit_frame,30,255,CV_THRESH_BINARY);
		last_frame=cvCloneImage(this_frame);
		// calculate white pixels (pixel shift)
		int pixel_shift = cvCountNonZero(bit_frame);
		//cvSaveImage("diff.png", bit_frame);
		cout << pixel_shift << endl;

		// Fetch object -> image, world->image and world -> object matrices
		CvMat *proj = multi->model.augm.GetProjectionMatrix(current_cam);
		CvMat *old_proj = multi->model.augm.GetProjectionMatrix(current_cam);
		CvMat *world = multi->model.augm.GetObjectToWorld();
		Mat3x4 moveObject, rot, obj2World, movedRT_;
		moveObject.setTranslate(im->width/2,im->height/2,-120*3/4);
		rot.setRotate(Vec3(1,0,0),2*M_PI*180.0/360.0);
		moveObject.mul(rot);
		CvMat cvMoveObject = cvMat(3,4,CV_64FC1, moveObject.m);
		CvMat movedRT=cvMat(3,4,CV_64FC1,movedRT_.m);

		// pose only during movement
		if (pixel_shift >= 10000 || !have_proj)
		{	
			// memcpy or vectorisation speedup?
			for( int i = 0; i < 3; i++ )
			{
				for( int j = 0; j < 4; j++ ) 
				{
					a_proj[i][j] = cvmGet( proj, i, j );
					obj2World.m[i][j] = cvmGet(world, i, j);
				}
			}
			have_proj = 1;
		    memcpy(old_a_proj, a_proj, sizeof(a_proj)); 
		}
		else // copy last known good projection over current
		{
		    memcpy(a_proj, old_a_proj, sizeof(old_a_proj)); 
		}

		CamCalibration::Mat3x4Mul( world, &cvMoveObject, &movedRT);
		// translate into OpenGL PROJECTION and MODELVIEW matrices
		PerspectiveCamera c;
		c.loadTdir(a_proj, multi->cams[0]->frame->width, multi->cams[0]->frame->height);
		c.flip();
		c.setPlanes(100,1000000); // near/far clip planes
		cvReleaseMat(&proj);

		// must set the model view after drawing the background.
		c.setGlProjection();		
		c.setGlModelView();

		/*! this is the beginning of prototype code for an occlusion mask built 
		 *  by comparison of the tracked plane with that of the model image

		// create a copy of frame texture and warp to model image perspective
		CvPoint2D32f *c2 = new CvPoint2D32f[4];
		// update corner points of ROI in pixel space
		c2[0].x = cvRound(multi->cams[0]->detector.detected_u_corner1);
		c2[0].y = cvRound(multi->cams[0]->detector.detected_v_corner1);
		c2[1].x = cvRound(multi->cams[0]->detector.detected_u_corner2);
		c2[1].y = cvRound(multi->cams[0]->detector.detected_v_corner2);
		c2[2].x = cvRound(multi->cams[0]->detector.detected_u_corner3);
		c2[2].y = cvRound(multi->cams[0]->detector.detected_v_corner3);
		c2[3].x = cvRound(multi->cams[0]->detector.detected_u_corner4);
		c2[3].y = cvRound(multi->cams[0]->detector.detected_v_corner4);

		CvMat* mmat = cvCreateMat(3,3, CV_32FC1);
		IplImage *warped = cvCreateImage(cvSize(model_image->width, model_image->height), 8, 3);
		mmat = cvGetPerspectiveTransform(c2, c1, mmat);
		cvWarpPerspective(frameTexture->getIm(), warped, mmat);
		cvReleaseMat(&mmat);

		// find difference between model image and frame		
		IplImage *i1=cvCreateImage(cvSize(im->width,im->height),im->depth,1);
		IplImage *i2=cvCreateImage(cvSize(im->width,im->height),im->depth,1);
		IplImage *diff=cvCreateImage(cvSize(im->width,im->height),im->depth,1);
		IplImage *display=cvCreateImage(cvSize(im->width,im->height),im->depth,1);

		cvCvtColor(im, i1,CV_BGR2GRAY);
		cvCvtColor(warped, i2,CV_BGR2GRAY);
		cvAbsDiff(i2,i1,diff);
		cvThreshold(diff, display, 35, 255, CV_THRESH_BINARY);

		cvReleaseImage(&warped);
		cvSaveImage("checkdiff.png", display);
		*/

		/* circles at corners of ROI. useful for debugging.
		cvCircle(frameTexture->getIm(), c1, 10, CV_RGB(255, 0, 0), 2); 
		cvCircle(frameTexture->getIm(), c2, 10, CV_RGB(255, 0, 0), 2); 
		cvCircle(frameTexture->getIm(), c3, 10, CV_RGB(255, 0, 0), 2); 
		cvCircle(frameTexture->getIm(), c4, 10, CV_RGB(255, 0, 0), 2); 
		
		tex = new IplTexture;
		tex->setImage(frameTexture->getIm());
		drawBackground(tex);
		*/

		

#ifndef DEBUG_SHADER

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_CULL_FACE);
		// multiply texture colour by surface colour of poly
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); 
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);	
		
		if (avi_play == true)
		{
			IplImage *avi_frame = 0;
			IplImage *avi_image = 0;
			avi_frame = cvQueryFrame( avi_capture );
			avi_image = cvCreateImage(cvSize(WIDTH/2, HEIGHT/2), 8, 3); 
			cvResize(avi_frame, avi_image, 0); 
			avi_image->origin = avi_frame->origin;
			GLenum format = IsBGR(avi_image->channelSeq) ? GL_BGR_EXT : GL_RGBA;

			if (!avi_play_init)
			{
				glGenTextures(1, &imageID);
				glBindTexture(GL_TEXTURE_2D, imageID);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, avi_image->width, avi_image->height, 0, format, GL_UNSIGNED_BYTE, avi_image->imageData);
				avi_play_init=true;
			}
		    else 
			{
				 glBindTexture(GL_TEXTURE_2D, imageID);
				 glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, avi_image->width, avi_image->height, format, GL_UNSIGNED_BYTE, avi_image->imageData);  
			}
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, imageID);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			IplImage image = *artverts[cnt].image;
    		GLenum format = IsBGR(image.channelSeq) ? GL_BGR_EXT : GL_RGBA;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, format, GL_UNSIGNED_BYTE, image.imageData);
		}
			
		glEnable(GL_TEXTURE_2D);

		glHint(GL_POLYGON_SMOOTH, GL_NICEST);
		glEnable(GL_POLYGON_SMOOTH);

		// create a simple fade-in when we have a track
		if (fade < 1.0)
		{
			fade += 0.1 * fade_speed;
		}
		else
		{
			fade = 1;
		}

		glColor4f(1.0, 1.0, 1.0, fade);

		glBegin(GL_QUADS);
			glTexCoord2f(0, 0);glVertex3f(roi_vec[0], roi_vec[1], 0);
			glTexCoord2f(1, 0);glVertex3f(roi_vec[2], roi_vec[3], 0);
			glTexCoord2f(1, 1);glVertex3f(roi_vec[4], roi_vec[5], 0);
			glTexCoord2f(0, 1);glVertex3f(roi_vec[6], roi_vec[7], 0);
		glEnd();

		glDisable(GL_TEXTURE_2D);

		/*! 'label' is a boolean set by the right mouse button and toggles the
		 * in-scene artvert label.
		 */ 

		if (label)
		{
			glBegin(GL_LINE_LOOP);
				glColor3f(0.0, 1.0, 0.0);
				glVertex3f(roi_vec[0]-10, roi_vec[1]-10, 0);
				glVertex3f(roi_vec[2]+10, roi_vec[3]-10, 0);
				glVertex3f(roi_vec[4]+10, roi_vec[5]+10, 0);
				glVertex3f(roi_vec[6]-10, roi_vec[7]+10, 0);
				glVertex3f(roi_vec[0]-10, roi_vec[1]-10, 0);
			glEnd();

			glTranslatef(roi_vec[2]+12, roi_vec[3], 0);
			glRotatef(180, 1.0, 0.0, 0.0);
			glRotatef(-45, 0.0, 1.0, 0.0);
			glColor4f(0.0, 1.0, 0.0, 1);

			glBegin(GL_LINE_LOOP);
				glVertex3f(0, 10, -.2);	
				glVertex3f(150, 10, -.2);	
				glVertex3f(150, -60, -.2);	
				glVertex3f(0, -60, -.2);	
			glEnd();

			glColor4f(0.0, 1.0, 0.0, .5);

			glBegin(GL_QUADS);
				glVertex3f(0, 10, -.2);	
				glVertex3f(150, 10, -.2);	
				glVertex3f(150, -60, -.2);	
				glVertex3f(0, -60, -.2);	
			glEnd();
			
			// render the text in the label
			glColor4f(1.0, 1.0, 1.0, 1);
			ftglFont->Render(artverts[cnt].artvert);
			glTranslatef(0, -12, 0);
			ftglFont->Render(artverts[cnt].date);
			glTranslatef(0, -12, 0);
			ftglFont->Render(artverts[cnt].author);
			glTranslatef(0, -12, 0);
			ftglFont->Render(artverts[cnt].advert);
			glTranslatef(0, -12, 0);
			ftglFont->Render(artverts[cnt].street);
		}

#else
#endif
		//glEnd();

#ifndef DEBUG_SHADER
		// apply the object transformation matrix
		Mat3x4 w2e(c.getWorldToEyeMat());
		w2e.mul(moveObject);
		c.setWorldToEyeMat(w2e);
		c.setGlModelView();
#endif

		if (multi->model.map.isReady()) {
			glDisable(GL_LIGHTING);
#ifdef DEBUG_SHADER
			multi->model.map.enableShader(current_cam, world);
#else
			multi->model.map.enableShader(current_cam, &movedRT);
#endif
		} 
		cvReleaseMat(&world);
		{
			CvScalar c =cvGet2D(multi->model.image, multi->model.image->height/2, multi->model.image->width/2);
			glColor3d(c.val[2], c.val[1], c.val[0]);
		}
		if (multi->model.map.isReady())
			multi->model.map.disableShader();
		else
			glDisable(GL_LIGHTING);

		if ( avi_play == true  ) 
		{
			cvReleaseImage(&avi_image);
			cvReleaseImage(&avi_frame);
		}
	}

	else if (!frame_ok)
	{
		fade = 0.0;
		found_match=0;
	}

    glLoadIdentity();
	// we need to setup a new projection matrix for the title font.
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(-.98, 0.9, 0.0);
	glScalef(.003, .003, .003);
	ftglFont->FaceSize(32);
	glColor4f(1.0, 1.0, 1.0, 1);
	ftglFont->Render("the artvertiser 0.4");
	glTranslatef(0, -(HEIGHT+30), 0);
	glColor4f(1.0, 1.0, 1.0, .6);
	//ftglFont->FaceSize(16);
	//ftglFont->Render(date(now).c_str());
	if (found_match == 1 and (now/1000)%2== 0)
	{
		glTranslatef(WIDTH-295, HEIGHT+35, 0);
		glColor4f(0.0, 1.0, 0.0, .8);
		glBegin(GL_TRIANGLES);
			glVertex3f(140, 0, 0);	
			glVertex3f(150, 10, 0);	
			glVertex3f(140, 20, 0);	
		glEnd();
		glTranslatef(70, 5, 0);
		ftglFont->FaceSize(16);
		ftglFont->Render("tracking");
	}

	// reset the ftgl font size for next pass
	ftglFont->FaceSize(12);

	glutSwapBuffers();
	cvReleaseImage(&image); // cleanup used image
	glFlush();
}

/*! GLUT calls this during photometric calibration or augmentation phase when
 * there's nothing else to do. This function does the 2D detection and bundle
 * adjusts the 3D pose of the calibration pattern. 
 */
static void idle()
{
	// acquire images
	multi->grabFrames();

	// detect the calibration object in every image
	// (this loop could be paralelized)
	int nbdet=1;

	multi->cams[0]->detect();

	if(!frameTexture) frameTexture = new IplTexture;
	frameTexture->setImage(multi->cams[current_cam]->frame);

	frame_ok=false;

	multi->model.augm.Clear();
	if (multi->cams[0]->detector.object_is_detected) 
	{
		add_detected_homography(0, multi->cams[0]->detector, multi->model.augm);
	} 
	else 
	{
		multi->model.augm.AddHomography();
	}

	frame_ok = multi->model.augm.Accomodate(4, 1e-4);

	if (frame_ok) {
		// fetch surface normal in world coordinates
		CvMat *mat = multi->model.augm.GetObjectToWorld();
		float normal[3];
		for (int j=0;j<3;j++) normal[j] = cvGet2D(mat, j, 2).val[0];
		cvReleaseMat(&mat);
	}
	glutPostRedisplay();
}

//! Starts photometric calibration.
static void start()
{
	glutIdleFunc(idle);
	glutDisplayFunc(draw);
}
//EOF
