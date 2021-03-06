#include <opencv2/opencv.hpp>

#include <string>
#include <iostream>
#include <fstream>
#include <vector>

#include <time.h>

using namespace cv;
using namespace cv::ml;
using namespace std;

void get_svm_detector(const Ptr<SVM>& svm, vector< float > & hog_detector );
void convert_to_ml(const std::vector< cv::Mat > & train_samples, cv::Mat& trainData );
void load_images( const string & prefix, const string & filename, vector< Mat > & img_lst );
void sample_neg( const vector< Mat > & full_neg_lst, vector< Mat > & neg_lst, const Size & size, int neg_steps=1 );
Mat get_hogdescriptor_visu(const Mat& color_origImg, vector<float>& descriptorValues, const Size & size );
void compute_hog( const vector< Mat > & img_lst, vector< Mat > & gradient_lst, const Size & size );
void train_svm( const vector< Mat > & gradient_lst, const vector< int > & labels );
void draw_locations( Mat & img, const vector< Rect > & locations, const Scalar & color );
void test_it( const Size & size );

void get_svm_detector(const Ptr<SVM>& svm, vector< float > & hog_detector )
{
    // get the support vectors
    Mat sv = svm->getSupportVectors();
    const int sv_total = sv.rows;
    // get the decision function
    Mat alpha, svidx;
    double rho = svm->getDecisionFunction(0, alpha, svidx);

    CV_Assert( alpha.total() == 1 && svidx.total() == 1 && sv_total == 1 );
    CV_Assert( (alpha.type() == CV_64F && alpha.at<double>(0) == 1.) ||
               (alpha.type() == CV_32F && alpha.at<float>(0) == 1.f) );
    CV_Assert( sv.type() == CV_32F );
    hog_detector.clear();

    hog_detector.resize(sv.cols + 1);
    memcpy(&hog_detector[0], sv.ptr(), sv.cols*sizeof(hog_detector[0]));
    hog_detector[sv.cols] = (float)-rho;
}


/*
* Convert training/testing set to be used by OpenCV Machine Learning algorithms.
* TrainData is a matrix of size (#samples x max(#cols,#rows) per samples), in 32FC1.
* Transposition of samples are made if needed.
*/
void convert_to_ml(const std::vector< cv::Mat > & train_samples, cv::Mat& trainData )
{
    //--Convert data
    const int rows = (int)train_samples.size();
    const int cols = (int)std::max( train_samples[0].cols, train_samples[0].rows );
    cv::Mat tmp(1, cols, CV_32FC1); //< used for transposition if needed
    trainData = cv::Mat(rows, cols, CV_32FC1 );
    vector< Mat >::const_iterator itr = train_samples.begin();
    vector< Mat >::const_iterator end = train_samples.end();
    for( int i = 0 ; itr != end ; ++itr, ++i )
    {
        CV_Assert( itr->cols == 1 ||
            itr->rows == 1 );
        if( itr->cols == 1 )
        {
            transpose( *(itr), tmp );
            tmp.copyTo( trainData.row( i ) );
        }
        else if( itr->rows == 1 )
        {
            itr->copyTo( trainData.row( i ) );
        }
    }
}

void load_images( const String & prefix, vector< Mat > & img_lst )
{
    vector<String> fileNames;
    glob( prefix, fileNames, true );

    for ( size_t i=0; i<fileNames.size(); i++ )
    {
        Mat img = imread( fileNames[i] ); // load the image
        if ( img.empty() ) // invalid image, just skip it.
        {
            cerr << "invalid image: " << fileNames[i] << endl;
            continue;
        }
#ifdef _DEBUG
        imshow( "image", img );
        waitKey( 10 );
#endif
        img_lst.push_back( img );
    }
}

void sample_neg( const vector< Mat > & full_neg_lst, vector< Mat > & neg_lst, const Size & size, int neg_steps )
{
    Rect box(0, 0, size.width, size.height);
    vector< Mat >::const_iterator img = full_neg_lst.begin();
    vector< Mat >::const_iterator end = full_neg_lst.end();
    for( ; img != end ; ++img )
    {
        if (neg_steps==0)
        {
            Mat roi = (*img)(box);
            neg_lst.push_back( roi.clone() );
            continue;
        }
        for (int i=0; i < img->rows - neg_steps - box.height; i+=neg_steps)
        for (int j=0; j < img->cols - neg_steps - box.width;  j+=neg_steps)
        {
            box.x = j;
            box.y = i;
            Mat roi = (*img)(box);
            neg_lst.push_back( roi.clone() );
#ifdef _DEBUG
            imshow( "img", roi.clone() );
            waitKey( 10 );
#endif
        }
    }
}

// From http://www.juergenwiki.de/work/wiki/doku.php?id=public:hog_descriptor_computation_and_visualization
Mat get_hogdescriptor_visu(const Mat& color_origImg, vector<float>& descriptorValues, const Size & size )
{
    const int DIMX = size.width;
    const int DIMY = size.height;
    float zoomFac = 3;
    Mat visu;
    resize(color_origImg, visu, Size( (int)(color_origImg.cols*zoomFac), (int)(color_origImg.rows*zoomFac) ) );

    int cellSize        = 8;
    int gradientBinSize = 9;
    float radRangeForOneBin = (float)(CV_PI/(float)gradientBinSize); // dividing 180 into 9 bins, how large (in rad) is one bin?

    // prepare data structure: 9 orientation / gradient strenghts for each cell
    int cells_in_x_dir = DIMX / cellSize;
    int cells_in_y_dir = DIMY / cellSize;
    float*** gradientStrengths = new float**[cells_in_y_dir];
    int** cellUpdateCounter   = new int*[cells_in_y_dir];
    for (int y=0; y<cells_in_y_dir; y++)
    {
        gradientStrengths[y] = new float*[cells_in_x_dir];
        cellUpdateCounter[y] = new int[cells_in_x_dir];
        for (int x=0; x<cells_in_x_dir; x++)
        {
            gradientStrengths[y][x] = new float[gradientBinSize];
            cellUpdateCounter[y][x] = 0;

            for (int bin=0; bin<gradientBinSize; bin++)
                gradientStrengths[y][x][bin] = 0.0;
        }
    }

    // nr of blocks = nr of cells - 1
    // since there is a new block on each cell (overlapping blocks!) but the last one
    int blocks_in_x_dir = cells_in_x_dir - 1;
    int blocks_in_y_dir = cells_in_y_dir - 1;

    // compute gradient strengths per cell
    int descriptorDataIdx = 0;
    int cellx = 0;
    int celly = 0;

    for (int blockx=0; blockx<blocks_in_x_dir; blockx++)
    {
        for (int blocky=0; blocky<blocks_in_y_dir; blocky++)
        {
            // 4 cells per block ...
            for (int cellNr=0; cellNr<4; cellNr++)
            {
                // compute corresponding cell nr
                cellx = blockx;
                celly = blocky;
                if (cellNr==1) celly++;
                if (cellNr==2) cellx++;
                if (cellNr==3)
                {
                    cellx++;
                    celly++;
                }

                for (int bin=0; bin<gradientBinSize; bin++)
                {
                    float gradientStrength = descriptorValues[ descriptorDataIdx ];
                    descriptorDataIdx++;

                    gradientStrengths[celly][cellx][bin] += gradientStrength;

                } // for (all bins)


                // note: overlapping blocks lead to multiple updates of this sum!
                // we therefore keep track how often a cell was updated,
                // to compute average gradient strengths
                cellUpdateCounter[celly][cellx]++;

            } // for (all cells)


        } // for (all block x pos)
    } // for (all block y pos)


    // compute average gradient strengths
    for (celly=0; celly<cells_in_y_dir; celly++)
    {
        for (cellx=0; cellx<cells_in_x_dir; cellx++)
        {

            float NrUpdatesForThisCell = (float)cellUpdateCounter[celly][cellx];

            // compute average gradient strenghts for each gradient bin direction
            for (int bin=0; bin<gradientBinSize; bin++)
            {
                gradientStrengths[celly][cellx][bin] /= NrUpdatesForThisCell;
            }
        }
    }

    // draw cells
    for (celly=0; celly<cells_in_y_dir; celly++)
    {
        for (cellx=0; cellx<cells_in_x_dir; cellx++)
        {
            int drawX = cellx * cellSize;
            int drawY = celly * cellSize;

            int mx = drawX + cellSize/2;
            int my = drawY + cellSize/2;

            rectangle(visu, Point((int)(drawX*zoomFac), (int)(drawY*zoomFac)), Point((int)((drawX+cellSize)*zoomFac), (int)((drawY+cellSize)*zoomFac)), Scalar(100,100,100), 1);

            // draw in each cell all 9 gradient strengths
            for (int bin=0; bin<gradientBinSize; bin++)
            {
                float currentGradStrength = gradientStrengths[celly][cellx][bin];

                // no line to draw?
                if (currentGradStrength==0)
                    continue;

                float currRad = bin * radRangeForOneBin + radRangeForOneBin/2;

                float dirVecX = cos( currRad );
                float dirVecY = sin( currRad );
                float maxVecLen = (float)(cellSize/2.f);
                float scale = 2.5; // just a visualization scale, to see the lines better

                // compute line coordinates
                float x1 = mx - dirVecX * currentGradStrength * maxVecLen * scale;
                float y1 = my - dirVecY * currentGradStrength * maxVecLen * scale;
                float x2 = mx + dirVecX * currentGradStrength * maxVecLen * scale;
                float y2 = my + dirVecY * currentGradStrength * maxVecLen * scale;

                // draw gradient visualization
                line(visu, Point((int)(x1*zoomFac),(int)(y1*zoomFac)), Point((int)(x2*zoomFac),(int)(y2*zoomFac)), Scalar(0,255,0), 1);

            } // for (all bins)

        } // for (cellx)
    } // for (celly)


    // don't forget to free memory allocated by helper data structures!
    for (int y=0; y<cells_in_y_dir; y++)
    {
        for (int x=0; x<cells_in_x_dir; x++)
        {
            delete[] gradientStrengths[y][x];
        }
        delete[] gradientStrengths[y];
        delete[] cellUpdateCounter[y];
    }
    delete[] gradientStrengths;
    delete[] cellUpdateCounter;

    return visu;

} // get_hogdescriptor_visu

void compute_hog( const vector< Mat > & img_lst, vector< Mat > & gradient_lst, const Size & size )
{
    HOGDescriptor hog;
    hog.winSize = size;
    Mat gray;
    vector< Point > location;
    vector< float > descriptors;

    vector< Mat >::const_iterator img = img_lst.begin();
    vector< Mat >::const_iterator end = img_lst.end();
    for( ; img != end ; ++img )
    {
        cvtColor( *img, gray, COLOR_BGR2GRAY );
        hog.compute( gray, descriptors, Size( 8, 8 ), Size( 0, 0 ), location );
        gradient_lst.push_back( Mat( descriptors ).clone() );
#ifdef _DEBUG
        imshow( "gradient", get_hogdescriptor_visu( img->clone(), descriptors, size ) );
        waitKey( 10 );
#endif
    }
}

void train_svm( const vector< Mat > & gradient_lst, const vector< int > & labels )
{

    Mat train_data;
    convert_to_ml( gradient_lst, train_data );

    clog << "Start training...";
    Ptr<SVM> svm = SVM::create();
    /* Default values to train SVM */
    //svm->setCoef0(0.0);
    //svm->setDegree(3);
    svm->setTermCriteria(TermCriteria( CV_TERMCRIT_ITER+CV_TERMCRIT_EPS, 1000, 1e-3 ));
    //svm->setGamma(0);
    svm->setKernel(SVM::LINEAR);
    //svm->setNu(0.5);
    svm->setP(0.1); // for EPSILON_SVR, epsilon in loss function?
    svm->setC(0.01); // From paper, soft classifier
    svm->setType(SVM::EPS_SVR); // C_SVC; // EPSILON_SVR; // may be also NU_SVR; // do regression task
    svm->train(train_data, ROW_SAMPLE, Mat(labels));
    clog << "...[done]" << endl;

    svm->save( "my_people_detector.yml" );
}

void draw_locations( Mat & img, const vector< Rect > & locations, const Scalar & color )
{
    if( !locations.empty() )
    {
        vector< Rect >::const_iterator loc = locations.begin();
        vector< Rect >::const_iterator end = locations.end();
        for( ; loc != end ; ++loc )
        {
            rectangle( img, *loc, color, 2 );
        }
    }
}

void test_it( const Size & size ,
    const vector< Mat > &pos_lst,
    const vector< Mat > &full_neg_lst, int n)
{
    HOGDescriptor hog;
    hog.winSize = size;
    Scalar reference( 0, 255, 0 );
    // Load the trained SVM.
    Ptr<ml::SVM> svm = StatModel::load<SVM>( "my_people_detector.yml" );
    vector< float > hog_detector;
    get_svm_detector( svm, hog_detector );
    hog.setSVMDetector( hog_detector );
    for (int i=0; i<n; i++)
    {
        Mat img = full_neg_lst[i];
        Rect box(
            rand() % (img.cols - size.width),
            rand() % (img.rows - size.height),
            size.width,
            size.height
            );
        Mat roi(img, box);
        pos_lst[i].copyTo(roi);
        vector< Rect > locations;
        hog.detectMultiScale( img, locations, 0.004 );
        Mat draw = img.clone();
        rectangle( draw, box, Scalar(0,0,200), 3 );
        draw_locations( draw, locations, reference );
        int hits = 0;
        double minD=99999;
        double maxD=0;
        for (size_t j=0; j<locations.size(); j++)
        {
            /*Rect inter = locations[j] & box;
            double a = inter.area();
            if (a==0) continue;
            minD = std::min(minD, a);
            maxD = std::max(maxD, a);
            hits += (inter.area() > (2*box.area())/3);
                 //&& (abs(locations[j].area()-box.area())<(box.area()/4));
            */

            Point d1 = box.tl() - locations[j].tl();
            Point d2 = box.br() - locations[j].br();
            double d = sqrt(d1.x*d1.x + d1.y*d1.y) + sqrt(d2.x*d2.x + d2.y*d2.y);
            hits += (d < box.area()/20);
            if (d<minD) minD=d;
            if (d>=maxD) maxD=d;
        }
        cerr << hits << " true pos of " << locations.size() << " " << minD << " " << maxD << endl;
        imshow("m", draw);
        if (waitKey(5000) == 27) break;
    }
}
void test_it( const Size & size )
{
    cerr << "press 'd' to toggle the default person detector" << endl;
    char key = 27;
    Scalar reference( 0, 255, 0 );
    Scalar trained( 0, 0, 255 );
    Mat img, draw;
    Ptr<SVM> svm;
    HOGDescriptor hog;
    HOGDescriptor my_hog;
    my_hog.winSize = size;
    VideoCapture video;
    vector< Rect > locations;
    bool showDefault = false;
    // Load the trained SVM.
    svm = StatModel::load<SVM>( "my_people_detector.yml" );
    // Set the trained svm to my_hog
    vector< float > hog_detector;
    get_svm_detector( svm, hog_detector );
    my_hog.setSVMDetector( hog_detector );
    // Set the people detector.
    hog.setSVMDetector( hog.getDefaultPeopleDetector() );
    // Open the camera.
    video.open(0);
    if( !video.isOpened() )
    {
        cerr << "Unable to open the device 0" << endl;
        exit( -1 );
    }

    bool end_of_process = false;
    while( !end_of_process )
    {
        video >> img;
        if( img.empty() )
            break;

        draw = img.clone();
        if (showDefault)
        {

            locations.clear();
            hog.detectMultiScale( img, locations );
            draw_locations( draw, locations, reference );
        }

        locations.clear();
        my_hog.detectMultiScale( img, locations );
        draw_locations( draw, locations, trained );

        imshow( "Video", draw );
        key = 255 & (char)waitKey( 10 );
        if( 27 == key )
            end_of_process = true;
        if( 'd' == key )
            showDefault = ! showDefault;
    }
}

int main( int argc, char** argv )
{
    cv::CommandLineParser parser(argc, argv,
        "{ help    h ||   show help message }"
        "{ test    t ||   test only }"
        "{ pos     p ||   folder with pos images like ~/img/*.png }"
        "{ neg     n ||   folder with neg images }"
        "{ steps   s |0|  step width for neg patches }"
        "{ mirror  m ||   mirror pos patches }"
        "{ width   W |64| window width }"
        "{ height  H |96| window height }");

    if (parser.has("help"))
    {
        parser.printMessage();
        exit(0);
    }
    vector< Mat > pos_lst;
    vector< Mat > full_neg_lst;
    vector< Mat > neg_lst;
    vector< Mat > gradient_lst;
    vector< int > labels;
    string pos_dir = parser.get<string>("pos");
    string neg_dir = parser.get<string>("neg");
    Size windowSize( parser.get<int>("width"),
                     parser.get<int>("height"));
    int neg_steps = parser.get<int>("steps"); // sample N images from a negative
    bool doTest = parser.has("test");
    bool doMirror = parser.has("mirror");

    if( pos_dir.empty() || neg_dir.empty() )
    {
        cout << "Wrong number of parameters." << endl
             << "Usage: " << argv[0] << " --pos=pos_dir --neg=neg_dir" << endl
             << "example: " << argv[0] << " --pos=/INRIA_dataset/*.jpg --neg=/my/bgimages/*.png" << endl;
        exit( -1 );
    }
    load_images( pos_dir, pos_lst );
    if (doMirror)
    {
        std::vector<Mat> v(pos_lst.begin(), pos_lst.end());
        for (Mat m : v)
        {
            Mat n;
            flip(m,n,1);
            pos_lst.push_back(n);
        }
    }
    labels.assign( pos_lst.size(), +1 );
    const unsigned int old = (unsigned int)labels.size();
    cout << (doTest?"testing":"training") << " with " << pos_lst.size() << " positive " << windowSize ;
    load_images( neg_dir, full_neg_lst );

    if (!doTest)
        sample_neg( full_neg_lst, neg_lst, windowSize, neg_steps );
    else
        neg_lst = full_neg_lst;

    labels.insert( labels.end(), neg_lst.size(), -1 );
    cout << " and " << neg_lst.size() << " negative images" << endl;
    CV_Assert( old < labels.size() );
    compute_hog( pos_lst, gradient_lst, windowSize );
    compute_hog( neg_lst, gradient_lst, windowSize );

    if (!doTest)
        train_svm( gradient_lst, labels );

    //test_it( windowSize ); // change with your parameters
    test_it( windowSize, pos_lst, full_neg_lst, 100 ); // change with your parameters

    return 0;
}
