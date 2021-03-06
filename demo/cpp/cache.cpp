#include "opencv2/highgui.hpp"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include <iostream>
using namespace cv;
using namespace std;

int main( int argc, char** argv )
{
    int k=0;
    String c_in("c:\\Users\\ppp\\AppData\\Local\\Mozilla\\Firefox\\Profiles\\lm978hx3.default\\cache2\\entries");
    String c_out("c:\\data\\img\\cache\\");
    if (argc>1)
    {
        c_out += String(argv[1]);
        String cmd = String("@mkdir ") + c_out;
        system(cmd.c_str());
    }
    vector<String> fn;
    glob(c_in, fn, true);
    for(size_t i=0; i<fn.size(); i++)
    {
        String v2 = fn[i].substr(83);
        String n = c_out + v2 + ".png";
        Mat im = imread(fn[i]);

        if ((! im.empty())
            && (im.rows >= 256)
            && (im.cols >= 256)
            && imwrite(n, im))
        {
            cerr << "+ " << n << endl;
            k ++;
        }
        else
            cerr << "- " << n << endl;

    }
    cerr << "saved " << k << " images." << endl;
    return 0;
}
