/*	Auto-Stitch
	Brent Champion
	Nathaniel Pauze
*/

#include <iostream>
#include <filesystem>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp> // OpenCV Core Functionality
#include <opencv2/highgui/highgui.hpp> // High-Level Graphical User Interface
#include "opencv2/features2d.hpp" 

using namespace std;
using namespace std::chrono;
using namespace cv;

/* ------------------------------ Global Settings ------------------------------ */

#define STEP1					1 //load images
#define STEP2					1	//compute matches bwtween all
#define STEP3					1 //find optimal "path" to stich all  images 
#define STEP4					1 //perform stiching
#define SAVE_OUTPUT				1

#define RESCALE_ON_LOAD			0.3 // Rescaling the images for faster program (1 is no rescalling, 0.5 is half, etc)
#define UNDISTORT_ON_LOAD		0
#define SMART_ADD_GAUSIAN_BLUR	101

#define PIXEL_PADDING			600 //how many pixels should pad each image 
#define PADDING_AMMOUNT			2
#define PADDING_OFFSET			2

#define ORB_POINT_COUNT		500 //how many orb poitns to find

// Image Debug Flags
#define IMAGE_LOADING_DEBUG		1 // Show loaded image (original)
#define IMAGE_SMART_ADD_DEBUG	1 // Shows the masks of images 
#define IMAGE_MATCHING_DEBUG	1 //tranfomation matrixes, etc - nice
#define IMAGE_MATCHING_DISPLAY  0 //Shows matched points
#define IMAGE_COMPOSITE_DEBUG	1 // Shows each step of the composition of the final image

// Console Printing Flags
#define PRINT_CONSOLE_DEBUG		1 // Printing general info in console - leave on to see where program is
#define PRINT_CAMERA_DEBUG		1 // Printing camera matrix information
#define PRINT_PADDING_DEBUG		1 // Printing padding procedure information
#define PRINT_MATCHES_DEBUG		1 // Printing match scores 

// File Settings
#define SAVE_MATCH_SCORES		1
#define SAVE_COMPOSITE			1
#define FOLDER					2
#define MAX_IMAGES_TO_LOAD		4 // We should add something for all? Idk how though 

/* ------------------------------ Global Variables ------------------------------ */

/* Folder Path Settings
1 - Office
2 - WLH
3 - StJames
4- Nat's dirty room (Room)
*/

string folderPath; // Global for folder path
vector<int> imagesInComposite; // Indices of the images in the composite
double imageMatchingThreshold;

/* ------------------------------ Global Data Structures ------------------------------ */

struct PathNode {
	int path[2]; // Point A to Point B :) 
};

typedef struct PathNode PathNode;
typedef vector<PathNode> Path;

Path compositeImagePath; // Nice

/* ------------------------------ Function Protocols ------------------------------ */

// File management 
void setFolderPath();
bool importImages(string folderPath);
bool saveResult(Mat& src, string filename);
bool saveMatches(string filename);

// Preprocessing :) 
Mat addImagePadding(Mat& img);
Mat translateImg(Mat& img, Mat& target, int offsetx, int offsety);

// Step 1 - Match finding
void FindMatches(int img1indx, int img2indx);

// Step 2 - Transformation estimation
void solveTransforms(vector<Point2d>& transformPtsImg1, vector<Point2d>& transformPtsImg2, int img1indx, int img2indx);

// Step 3 - Composite Image Generation
int findCenterImage(); //get the index of the image with the least weights to it 
Path generateAssemblyPath(); // Generate the optimal assembly path -> This would be cool but could also be hardcoded..
Mat composite2Images(Mat& composite, int img1indx, int img2indx, bool useImageSpecified, Mat& imageSpecified);
Mat smartAddImg(Mat& img_1, Mat& img_2);

/* ------------------------------ Global Classes --------------------------------- */
class subImage {
public:
	string path; // File path
	string name; // File name
	Mat img; // Image source
	Mat imgGrey; // Gray Image
	vector<vector<DMatch>> goodMatches; // Matrix of all of the matches 
	vector<double> goodMatchScores; // List of all the goodMatchScores
	vector<Mat> homographyMatrixes; // List of all the homographies to other mats

	// subImage Constructor
	subImage(string path) {
		goodMatches.resize(MAX_IMAGES_TO_LOAD);
		goodMatchScores.resize(MAX_IMAGES_TO_LOAD);
		homographyMatrixes.resize(MAX_IMAGES_TO_LOAD);

		this->path = path;

		//load the image, but center it with a black border half its size
		Mat distorted = imread(path);
		Mat temp = Mat(distorted.rows, distorted.cols, distorted.type());
		//rescale if specified
		if (RESCALE_ON_LOAD != 1) {
			resize(distorted, distorted, Size(), RESCALE_ON_LOAD, RESCALE_ON_LOAD);
		}

		// Undistort the image with the camera matrix -- major key
		if (UNDISTORT_ON_LOAD) {
			Mat intrinsic = (Mat_<double>(3, 3) << 600, 0, distorted.cols / 2, 0, 600, distorted.rows / 2, 0, 0, 1); // ... 
			Mat distortionCoef = (Mat_<double>(1, 5) << 0.2, 0.05, 0.00, 0, 0); // We should make this a global setting
			Mat camMatrix = getOptimalNewCameraMatrix(intrinsic, distortionCoef, distorted.size(), 0);//make the actual transforma matrix 
			if (IMAGE_LOADING_DEBUG) {
				cout << "Camera matrix: \n" << camMatrix << endl;
			}
			temp = Mat(distorted.rows, distorted.cols, distorted.type());
			undistort(distorted, temp, camMatrix, distortionCoef);
		}
		else {
			temp = distorted;
		}

		this->img = addImagePadding(temp);
		//// center it with a black border PADDING_AMMOUNT its size
		//this->img = Mat(temp.rows * PADDING_AMMOUNT, temp.cols * PADDING_AMMOUNT, temp.type());
		//Mat trans_mat = (Mat_<double>(2, 3) << 1, 0, temp.cols / PADDING_OFFSET, 0, 1, temp.rows / PADDING_OFFSET);
		//warpAffine(temp, this->img, trans_mat, this->img.size());

		cvtColor(this->img, this->imgGrey, COLOR_RGB2GRAY);
	}
}; // Class storing details regarding an image

vector<subImage> imageSet; // Initialize a vector of all of the subImages -> to be combined into the 'super' image

/* --------------------------------- Main Routine ------------------------------------- */

int main(int argc, char* argv[]) {
	auto start = high_resolution_clock::now();
	Mat compositeImage;

	setFolderPath(); // Set the folder based for testing
	if (PRINT_CONSOLE_DEBUG) { // Initial steps
		cout << "\n Program running from directory: " << filesystem::current_path() << endl;
		cout << "\n Opening " << MAX_IMAGES_TO_LOAD << " images from " << folderPath << " folder \n" << endl;
	}

	if (!importImages(folderPath)) {
		cout << "Problem importing images!" << endl;
		return -1;
	}
	
	if (IMAGE_LOADING_DEBUG) { // Show the original images
		for (subImage img : imageSet) {
			namedWindow(img.path, WINDOW_NORMAL);
			imshow(img.path, img.img);
			resizeWindow(img.path, 600, 600);
		}
	}

	if (STEP1) { // Match Features
		auto startStep = high_resolution_clock::now();
		if (PRINT_CONSOLE_DEBUG) { // Initial steps
			cout << "\n Beginning Step 1 - Feature Matching Process \n" << endl;
		}
		// Write modular matching algorithm here -> have it work with the data structure we defined
		// Method to filter through the match metric score in step 1, find transformations
		// Currently O(n^2) -> Could look to optimize using more advanced analytics between images? 
		for (int i = 0; i < imageSet.size(); i++) {
			for (int j = 0; j < imageSet.size(); j++) {
				if (j != i) {
					//if this is 0 we havent checked this pair yet
					if (imageSet[i].goodMatchScores[j] == 0) {
						FindMatches(i, j);

					}
				}
			}
		}
		auto stop = high_resolution_clock::now();
		auto duration = duration_cast<microseconds>(stop - startStep);
		cout << "Time taken for Step 1: " << duration.count() << endl; // Report how long it took
	}

	if (STEP2) { // Get transformations
		auto startStep = high_resolution_clock::now();
		if (PRINT_CONSOLE_DEBUG) { // Initial steps
			cout << "\n Beginning Step 2 - Generating Transformations \n" << endl;
		}
		// Some code to find transformations
		auto stop = high_resolution_clock::now();




		auto duration = duration_cast<microseconds>(stop - startStep);
		cout << "Time taken for Step 2: " << duration.count() << endl; // Report how long it took
	}
	
	if (STEP3) {
		auto startStep = high_resolution_clock::now();
		if (PRINT_CONSOLE_DEBUG) { // Initial steps
			cout << "\n Beginning Step 3 - Generating composite image \n" << endl;
		}
		// dind center image
		int centerimgIndex = findCenterImage();
		cout << "Center img is img indx " << centerimgIndex << endl;

		// load midle image 
		compositeImage = imageSet[centerimgIndex].img; // Set composite to first image for now
		imagesInComposite.push_back(centerimgIndex);


		//all images that map to center image below threshold get added to composite
		for (int i = 0; i < imageSet.size(); i++) {
			if (i != centerimgIndex) {
				if (imageSet[i].goodMatchScores[centerimgIndex] < imageMatchingThreshold) {
					imagesInComposite.push_back(i);
					compositeImage = composite2Images(compositeImage, centerimgIndex, i,false,compositeImage);
				}
			}
		}
		
		vector<int> imagesInCompositeLevel2;
		//for all images that map to an image in the composite but no the center
		for (int i = 0; i < imageSet.size(); i++) {//check all images 
			for (int j = 1; j < imagesInComposite.size(); j++) {//with all in composite
				//dont map to center
				if (i != centerimgIndex) {
					//dont map to itself
					if (i != imagesInComposite[j]) {
						
						//make sure this one isnt in the lvl2 composite vector allready 
						//TODO I had one for lvl 1 composite too but it was causing a infinite loop
						bool flag = true;
						/*
						for (int x = 0; x < imagesInCompositeLevel2.size();x++) {
							if (i = imagesInCompositeLevel2[x]) {
								flag = false;
							}
						}*/
						if (flag) {
							//now if it maps to the one in the composite
							if (imageSet[imagesInComposite[j]].goodMatchScores[i] < imageMatchingThreshold) {
								Mat temp = Mat::zeros(compositeImage.rows, compositeImage.cols, compositeImage.type());
								temp = composite2Images(temp, imagesInComposite[j], i, false, compositeImage);
								string window = "temp of " + to_string(i) + " to " + to_string(imagesInComposite[j]);
								namedWindow(window, WINDOW_NORMAL);
								resizeWindow(window, 600, 600);
								imshow(window, temp);




								compositeImage = composite2Images(compositeImage, centerimgIndex, imagesInComposite[j], true, temp);
								imagesInCompositeLevel2.push_back(i);
								window = "final of " + to_string(imagesInComposite[j]) + " to " + to_string(centerimgIndex);
								namedWindow(window, WINDOW_NORMAL);
								resizeWindow(window, 600, 600);
								imshow(window, compositeImage);

							}
						}


					}

				}
			}
		}
		


		

		string window = "Final Composite Image of ";
		for (int i = 0; i < imagesInComposite.size(); i++) {
			window = window + to_string(imagesInComposite[i]) + ",";
		}
		window = window + "lv2: ";
		for (int i = 0; i < imagesInCompositeLevel2.size(); i++) {
			window = window + to_string(imagesInCompositeLevel2[i]) + ",";
		}

		namedWindow(window, WINDOW_NORMAL);
		resizeWindow(window, 600, 600);
		imshow(window, compositeImage);
		
		auto stop = high_resolution_clock::now();
		auto duration = duration_cast<microseconds>(stop - startStep);
		cout << "Time taken for Step 3: " << duration.count() << endl; // Report how long it took
	}

	if (SAVE_OUTPUT) {
		saveResult(compositeImage, "CompositeImage.jpg");
	}

	auto stop = high_resolution_clock::now();
	auto duration = duration_cast<microseconds>(stop - start);
	cout << "Total time taken: " << duration.count() << endl; // Report how long it took

	waitKey(0);
}

/* ----------------------------- Function Implementations ------------------------------*/

/* ------------ File Management ----------- */

void setFolderPath() {
	if (FOLDER == 1) {
		folderPath = "office2";
		imageMatchingThreshold = 3600;
	}
	else if (FOLDER == 2) {
		folderPath = "WLH";
		imageMatchingThreshold = 3000;
	}
	else if (FOLDER == 3) {
		folderPath = "StJames";
		imageMatchingThreshold = 3300;
	}
	else if (FOLDER == 4) {
		folderPath = "Room";
		imageMatchingThreshold = 3550;
	}
	else { cout << "Invalid FOLDER choice"; return; }
}

bool importImages(string folderPath) {
	try {
		int imagesLoaded = 0;
		for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
			if (imagesLoaded < MAX_IMAGES_TO_LOAD) {
				// cout << entry.path() << std::endl;
				imageSet.push_back(entry.path().string());//c++ is so weird, i just need to pass in the construtor params and not a new instance 
				imagesLoaded++;
			}
		} return true;
	}
	catch (const std::exception & e) {
		//probably couldnt find the folder
		cout << e.what() << endl;
			return false;
	}
}

bool saveResult(Mat& src, string filename) {
	try {
		imwrite(filename, src);
		return true;
	}
	catch (Exception & e) {
		cout << e.what() << endl;
		return false;
	}
}

bool saveMatches(string filename) {
	return true;
}


/* ------------- Preprocessing ------------ */

Mat translateImg(Mat& img, Mat& target, int offsetx, int offsety) {
	Mat trans_mat = (Mat_<double>(2, 3) << 1, 0, offsetx, 0, 1, offsety);
	warpAffine(img, target, trans_mat, img.size());
	return img;
}

Mat addImagePadding(Mat& img) {
	int currentPaddingTop = 0;
	int currentPaddingLeft = 0;
	int currentPaddingBottom = 0;
	int currentPaddingRight = 0;

	//so aparently the only good way to break out of nested loops in c++ is goto, go figure


	//start from top 
	for (int r = 0; r < img.rows; r++) {
		for (int c = 0; c < img.cols; c++) {
			if (img.at<Vec3b>(r, c) != Vec3b(0, 0, 0)) {
				currentPaddingTop = r;
				goto leftCheck;
			}
		}
	}
leftCheck:
	//start from left 
	for (int c = 0; c < img.cols; c++) {
		for (int r = 0; r < img.rows; r++) {
			if (img.at<Vec3b>(r, c) != Vec3b(0, 0, 0)) {
				currentPaddingLeft = c;
				goto bottomCheck;
			}
		}
	}
bottomCheck:
	//start from bottom
	for (int r = img.rows - 1; r >= 0; r--) {
		for (int c = 0; c < img.cols; c++) {
			if (img.at<Vec3b>(r, c) != Vec3b(0, 0, 0)) {
				currentPaddingBottom = img.rows - r - 1;
				goto rightCheck;
			}
		}
	}
rightCheck:
	//start from right
	for (int c = img.cols - 1; c >= 0; c--) {
		for (int r = 0; r < img.rows; r++) {
			if (img.at<Vec3b>(r, c) != Vec3b(0, 0, 0)) {
				currentPaddingRight = img.cols - c - 1;
				goto endOfChecks;
			}
		}
	}
endOfChecks:


	cout << "Image is " << img.rows << " rows by " << img.cols << " cols" << endl;
	cout << "Top padding: " << currentPaddingTop << " Left padding: " << currentPaddingLeft << " Bottom padding: " << currentPaddingBottom << " Right padding: " << currentPaddingRight << endl;


	int paddingNeededTop = 0;
	int paddingNeededLeft = 0;
	int paddingNeededBottom = 0;
	int paddingNeededRight = 0;
	if (currentPaddingTop < PIXEL_PADDING) {
		paddingNeededTop = PIXEL_PADDING - currentPaddingTop;
	}
	if (currentPaddingLeft < PIXEL_PADDING) {
		paddingNeededLeft = PIXEL_PADDING - currentPaddingLeft;
	}
	if (currentPaddingBottom < PIXEL_PADDING) {
		paddingNeededBottom = PIXEL_PADDING - currentPaddingBottom;
	}
	if (currentPaddingRight < PIXEL_PADDING) {
		paddingNeededRight = PIXEL_PADDING - currentPaddingRight;
	}
	int offsetx = paddingNeededLeft;
	int offsety = paddingNeededTop;

	Mat newImage = Mat::zeros(img.rows + paddingNeededTop + paddingNeededBottom, img.cols + paddingNeededLeft + paddingNeededRight, CV_8U);

	Mat trans_mat = (Mat_<double>(2, 3) << 1, 0, offsetx, 0, 1, offsety);
	warpAffine(img, newImage, trans_mat, newImage.size());
	cout << "new image dimensions " << newImage.rows << " rows by " << newImage.cols << " cols" << endl;
	return newImage;

}


/* --------------- Step 1 ----------------- */

void FindMatches(int img1indx, int img2indx) {
	Mat& img_1 = imageSet[img1indx].img;
	Mat& img_2 = imageSet[img2indx].img;
	
	double matchScore = 0;
	//intitate orb detector 
	vector<KeyPoint> keypoints_1, keypoints_2;
	Mat descriptors_1, descriptors_2;
	//Ptr<SIFT> detector = cv::xfeatures2d::SIFT::create;
	//Ptr<FeatureDetector> detector = ORB::create();
	Ptr<FeatureDetector> detector = ORB::create(ORB_POINT_COUNT, 1.2, 8, 127, 0, 2, ORB::HARRIS_SCORE, 127, 20);
	Ptr<DescriptorExtractor> descriptor = ORB::create();
	Ptr<DescriptorMatcher> matcher = DescriptorMatcher::create("BruteForce-Hamming");

	//detect points and compute descriptors
	detector->detect(img_1, keypoints_1);
	detector->detect(img_2, keypoints_2);
	descriptor->compute(img_1, keypoints_1, descriptors_1);
	descriptor->compute(img_2, keypoints_2, descriptors_2);

	//draw keypoints
	//Mat outimg1;
	//drawKeypoints(img_1, keypoints_1, outimg1, Scalar::all(-1), DrawMatchesFlags::DEFAULT);

	vector<DMatch> matches;
	//BFMatcher matcher ( NORM_HAMMING );
	matcher->match(descriptors_1, descriptors_2, matches);
	double min_dist = 10000, max_dist = 0;


	//Find the minimum and maximum distances between mathces, so the most similar and the least similar 
	for (int i = 0; i < descriptors_1.rows; i++)
	{
		double dist = matches[i].distance;
		if (dist < min_dist) min_dist = dist;
		if (dist > max_dist) max_dist = dist;
		matchScore += (1 + dist) * (1 + dist);
	}
	//match score is the average of all the distances
	matchScore = double(double(matchScore) / double(ORB_POINT_COUNT));


	printf("-Matches between img %d and %d -\n", img1indx, img2indx);
	//printf("-- Max dist : %f \n", max_dist);
	//printf("-- Min dist : %f \n", min_dist);
	printf("-- Match score : %f \n", matchScore);

	std::vector< DMatch > good_matches;
	//When the distance between the descriptors is 
	//greater than twice the minimum distance, the match is considered to be incorrect. 
	//But sometimes the minimum distance will be very small, and an empirical value of 30 is set as the lower limit.
	for (int i = 0; i < descriptors_1.rows; i++)
	{
		if (matches[i].distance <= max(2 * min_dist, 30.0))
		{
			good_matches.push_back(matches[i]);
		}
	}

	Mat img_goodmatch;
	//-- Draw results 
	if (IMAGE_MATCHING_DISPLAY) {

		//drawMatches(img_1, keypoints_1, img_2, keypoints_2, matches, img_match);
		drawMatches(img_1, keypoints_1, img_2, keypoints_2, good_matches, img_goodmatch);
		//namedWindow("matches", WINDOW_NORMAL);
		//imshow("matches", img_match);
		//resizeWindow("matches", 600, 600);
		string window = "good matches between " + to_string(img1indx) + " and " + to_string(img2indx);
		namedWindow(window, WINDOW_NORMAL);
		imshow(window, img_goodmatch);
		resizeWindow(window, 800, 800);
	}
	//estimate affine transfomation 
	//get points to use 
	if (IMAGE_MATCHING_DEBUG) {
		//cout << "points used to calc transform" << endl;
	}
	vector<Point2d> transformPtsImg1;
	vector<Point2d> transformPtsImg2;
	//get the good points, take top 60
	for (int i = 0; (i < good_matches.size() && i < 60); i++) {
		transformPtsImg1.push_back(keypoints_1[good_matches[i].queryIdx].pt);
		transformPtsImg2.push_back(keypoints_2[good_matches[i].trainIdx].pt);
		if (IMAGE_MATCHING_DEBUG) {
			//cout << transformPtsImg1[i] << " -> " << transformPtsImg2[i] << endl;
		}
	}
	//put the good points and the score of all points in the image set data
	imageSet[img1indx].goodMatches[img2indx] = good_matches;
	imageSet[img1indx].goodMatchScores[img2indx] = matchScore;
	imageSet[img2indx].goodMatches[img1indx] = good_matches;
	imageSet[img2indx].goodMatchScores[img1indx] = matchScore;

	//solve transforms if this match is good enough
	if (matchScore < imageMatchingThreshold + 50) {
		solveTransforms(transformPtsImg1, transformPtsImg2, img1indx, img2indx);
	}

}


/* --------------- Step 2 ----------------- */

void solveTransforms(vector<Point2d>& transformPtsImg1, vector<Point2d>& transformPtsImg2, int img1indx, int img2indx) {
	
	//find the transform
	Mat homo1 = findHomography(transformPtsImg2, transformPtsImg1, RANSAC, 5.0);
	if (IMAGE_MATCHING_DEBUG) {
		cout << "Transfomation Matrix" << endl;
		cout << homo1 << endl;
	}
	Mat homo2 = findHomography(transformPtsImg1, transformPtsImg2, RANSAC, 5.0);
	if (IMAGE_MATCHING_DEBUG) {
		cout << "Transfomation Matrix" << endl;
		cout << homo2 << endl;
	}
	//put in the dataset for later
	imageSet[img1indx].homographyMatrixes[img2indx] = homo1;
	imageSet[img2indx].homographyMatrixes[img1indx] = homo2;
}

/* --------------- Step 3 ----------------- */


int findCenterImage() {
	int minindex = 0;
	vector<int> sums;
	for (int i = 0; i < imageSet.size(); i++) {
		int sum = 0;
		for (int j = 0; j < imageSet.size(); j++) {
			sum += imageSet[i].goodMatchScores[j];
		}
		sums.push_back(sum);
	}
	for (int i = 0; i < imageSet.size(); i++) {
		if (sums[i] < sums[minindex]) {
			minindex = i;
		}
	}
	return minindex;
}

Path generateAssemblyPath() {
	// 
	PathNode test;
	test.path[0] = 1; test.path[1] = 1;
	Path assemblyPath;
	assemblyPath.push_back(test);

	return assemblyPath;
	
}

Mat composite2Images(Mat& composite, int img1indx, int img2indx,bool useImageSpecified,Mat& imageSpecified) {
	//Mat& img_1 = imageSet[img1indx].img;
	Mat& img_1 = composite;
	Mat& img_2 = imageSet[img2indx].img;
	if (useImageSpecified) {
		img_2 = imageSpecified;
	}
	
	Mat homo = imageSet[img1indx].homographyMatrixes[img2indx];

	//apply transformation to image 
	Mat warpedImg = Mat(img_2.rows, img_2.cols, img_2.type());
	warpPerspective(img_2, warpedImg, homo, warpedImg.size());

	if (IMAGE_MATCHING_DEBUG) {
		namedWindow("warpedIMG", WINDOW_NORMAL);
		imshow("warpedIMG", warpedImg);
		resizeWindow("warpedIMG", 800, 800);
	}

	//compose images
	Mat compositeImg;

	compositeImg = smartAddImg(img_1, warpedImg);
	//addWeighted(img_1, 0.5, warpedImg, 0.5, 1, compositeImg);
	//display
	string window = "composite Img using transform " + to_string(img2indx) + " to " + to_string(img1indx);
	namedWindow(window, WINDOW_NORMAL);
	imshow(window, compositeImg);
	resizeWindow(window, 800, 800);

	return compositeImg;
}

Mat smartAddImg(Mat& img_1, Mat& img_2) {
	//solid mask
	Mat solidMask = Mat::zeros(img_2.rows, img_2.cols, CV_8U);
	Mat erodedMask = Mat::zeros(img_2.rows, img_2.cols, CV_8U);
	for (int r = 0; r < img_2.rows; r++) {
		for (int c = 0; c < img_2.cols; c++) {
			if (img_2.at<Vec3b>(r, c) != Vec3b(0, 0, 0)) {
				solidMask.at<unsigned char>(r, c) = 255;
			}
		}
	}
	//erode it a bit (gets rid of fine black outline)
	int erosion_size = 10;
	Mat element = getStructuringElement(MORPH_RECT,
		Size(2 * erosion_size + 1, 2 * erosion_size + 1),
		Point(erosion_size, erosion_size));
	erode(solidMask, solidMask, element);
	//erode again so blur is smother transition, ei edges of blur dont start at grey and not black
	erosion_size = SMART_ADD_GAUSIAN_BLUR;
	element = getStructuringElement(MORPH_RECT,
		Size(2 * erosion_size + 1, 2 * erosion_size + 1),
		Point(erosion_size, erosion_size));
	erode(solidMask, erodedMask, element);


	//diplay if debug flag
	if (IMAGE_SMART_ADD_DEBUG) {
		namedWindow("solidMask", WINDOW_NORMAL);
		imshow("solidMask", solidMask);
		resizeWindow("solidMask", 600, 600);
	}
	//Blur the mask for use in blending, wraped this in a try cause blurs can be finicky  
	Mat bluredMask = Mat(img_2.rows, img_2.cols, CV_8U);
	try {
		GaussianBlur(erodedMask, bluredMask, Size(SMART_ADD_GAUSIAN_BLUR, SMART_ADD_GAUSIAN_BLUR), 0, 0);
		if (IMAGE_SMART_ADD_DEBUG) {
			namedWindow("bluredMask", WINDOW_NORMAL);
			imshow("bluredMask", bluredMask);
			resizeWindow("bluredMask", 600, 600);
		}
	}
	catch (const std::exception & e) {
		cout << "Blur error, somethign is wrong" << endl;
		cout << e.what() << endl;
		//bluredMask = solidMask;
	}
	//actually compute the new image on top of image 1 
	for (int r = 0; r < img_2.rows; r++) {
		for (int c = 0; c < img_2.cols; c++) {
			if (solidMask.at<unsigned char>(r, c) == 255) {
				Vec3b img2Pixel = img_2.at<Vec3b>(r, c);
				Vec3b img1Pixel = img_1.at<Vec3b>(r, c);
				if (img1Pixel != Vec3b(0, 0, 0)) {
					unsigned char mix = bluredMask.at<unsigned char>(r, c);
					img_1.at<Vec3b>(r, c) = (img1Pixel * double(double(255 - mix) / double(255))) + (img2Pixel * double(double(mix) / double(255)));
				}
				else {
					img_1.at<Vec3b>(r, c) = img2Pixel;
				}
			}

		}
	}
	return img_1;
}
