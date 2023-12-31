#include <iostream>
#include<boost/thread/thread.hpp>
#include<pcl/range_image/range_image.h>
#include<pcl/io/pcd_io.h>
#include<pcl/visualization/range_image_visualizer.h>
#include<pcl/visualization/pcl_visualizer.h>
#include<pcl/features/range_image_border_extractor.h>
#include<pcl/keypoints/narf_keypoint.h>
#include<pcl/console/parse.h>

typedef pcl::PointXYZ PointType;
using namespace std;

//parameters
float angular_resolution = 0.5f;
float support_size = 0.2f;
pcl::RangeImage::CoordinateFrame coordinate_frame = pcl::RangeImage::CAMERA_FRAME;
bool setUnseenToMaxRange = false;

void printUsage(const char *progName) {
    std::cout << "\n\nUsage:" << progName << "[options]<<scene.pcd>\n\n"
              << "Options:\n"
              << "--------------------------------------"
              << "-r <float>   angular resolution in degrees (default " << angular_resolution << ")\n"
              << "-c <int>     coordinate frame (default " << (int) coordinate_frame << ")\n"
              << "-m           Treat all unseen points to max range\n"
              << "-s <float>   support size for the interest points (diameter of the used sphere - "
              << "default " << support_size << ")\n"
              << "-h           this help\n"
              << "\n\n";
}

//main

int main(int argc, char **argv) {
    // --------------------------------------
    // -----Parse Command Line Arguments-----
    // --------------------------------------
    if (pcl::console::find_argument(argc, argv, "-h") >= 0)//如果输入参数是“-h”
    {
        printUsage(argv[0]);
        return 0;
    }
    if (pcl::console::find_argument(argc, argv, "-m") >= 0)//如果输入参数是“-m”
    {
        setUnseenToMaxRange = true;
        cout << "setting unseen values in range image to maximum range readings\n" << std::endl;
    }

    int tmp_coordinate_frame;
    if (pcl::console::parse(argc, argv, "-c", tmp_coordinate_frame) >= 0) {
        coordinate_frame = pcl::RangeImage::CoordinateFrame(tmp_coordinate_frame);
        cout << "using coordinate frame" << (int) coordinate_frame << ".\n";
    }
    if (pcl::console::parse(argc, argv, "-s", support_size) >= 0)
        cout << "Setting suport size to" << support_size << ".\n";
    if (pcl::console::parse(argc, argv, "-r", angular_resolution) >= 0)
        cout << "setting angular resolution to" << angular_resolution << "deg.\n";
    angular_resolution = pcl::deg2rad(angular_resolution);

    // ------------------------------------------------------------------
    // -----Read pcd file or create example point cloud if not given-----
    // ------------------------------------------------------------------

    pcl::PointCloud<PointType>::Ptr point_cloud_ptr(new pcl::PointCloud<PointType>);
    pcl::PointCloud<PointType> &point_cloud = *point_cloud_ptr;
    pcl::PointCloud<pcl::PointWithViewpoint> far_ranges;
    Eigen::Affine3f scene_sensor_pose(Eigen::Affine3f::Identity());
    std::vector<int> pcd_filename_indices = pcl::console::parse_file_extension_argument(argc, argv, "pcd");
    if (!pcd_filename_indices.empty())//
    {
        std::string filename = argv[pcd_filename_indices[0]];
        if (pcl::io::loadPCDFile(filename, point_cloud) == -1) {
            cout << "was not able to open file\"" << filename << "\".n";
            printUsage(argv[0]);
            return 0;
        }
        scene_sensor_pose = Eigen::Affine3f(Eigen::Translation3f(point_cloud.sensor_origin_[0],
                                                                 point_cloud.sensor_origin_[1],
                                                                 point_cloud.sensor_origin_[2])) *
                            Eigen::Affine3f(point_cloud.sensor_orientation_);//传感器取向
        std::string far_ranges_filename = pcl::getFilenameWithoutExtension(filename) + "_far_ranges.pcd";

        if (pcl::io::loadPCDFile(far_ranges_filename.c_str(), far_ranges) == -1)
            std::cout << "far ranges file\"" << far_ranges_filename << "\" does not exists.\n";
    } else {//创建点云
        setUnseenToMaxRange = true;
        cout << "\nNo *.pcd file given =>Generationg example point cloud.\n\n";
        for (float x = -0.5f; x <= 0.5f; x += 0.01f) {
            for (float y = -0.5f; y <= 0.5f; y += 0.01f) {
                PointType point;
                point.x = x;
                point.y = y;
                point.z = 2.0f - y;
                point_cloud.points.push_back(point);
            }
        }
        point_cloud.width = (int) point_cloud.points.size();
        point_cloud.height = 1;
    }

    // -----------------------------------------------
    // -----Create RangeImage from the PointCloud-----
    // -----------------------------------------------

    float noise_level = 0.0;
    float min_range = 0.0f;
    int border_size = 1;
    boost::shared_ptr<pcl::RangeImage> range_image_ptr(new pcl::RangeImage);
    pcl::RangeImage &range_image = *range_image_ptr;
    //从点云创建深度图
    range_image.createFromPointCloud(point_cloud,
                                     angular_resolution,
                                     pcl::deg2rad(120.0f),
                                     pcl::deg2rad(90.0f),
                                     scene_sensor_pose,
                                     coordinate_frame,
                                     noise_level,
                                     min_range,
                                     border_size);
    range_image.integrateFarRanges(far_ranges);
    if (setUnseenToMaxRange)
        range_image.setUnseenToMaxRange();

    //open 3D viewer and add point cloud
    pcl::visualization::PCLVisualizer viewer("3D viewer");
    viewer.setBackgroundColor(1, 1, 1);
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointWithRange> range_image_color_handler(range_image_ptr,
                                                                                                    100, 100, 100);
    viewer.addPointCloud(range_image_ptr, range_image_color_handler, "range image");
    viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 5, "range image");//设定渲染属性
    viewer.addCoordinateSystem(1.0f, "global");
    viewer.initCameraParameters();


    pcl::visualization::RangeImageVisualizer range_image_widget("Range image");
    range_image_widget.showRangeImage(range_image);


    //extract NARF borders 提取边界
    pcl::RangeImageBorderExtractor range_image_border_extractor;
    pcl::NarfKeypoint narf_keypoint_detector(&range_image_border_extractor);
    narf_keypoint_detector.setRangeImage(&range_image);
    narf_keypoint_detector.getParameters().support_size = support_size;

    pcl::PointCloud<int> keypoint_indices;//指数
    narf_keypoint_detector.compute(keypoint_indices);//wrong in here
    std::cout << "Found  " << keypoint_indices.points.size() << "  key points.\n";

    //sho points in 3D viewer
    pcl::PointCloud<pcl::PointXYZ>::Ptr keypoints_ptr(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ> &keypoints = *keypoints_ptr;
    keypoints.points.resize(keypoint_indices.points.size());
    for (size_t i = 0; i < keypoint_indices.points.size(); ++i)
        keypoints.points[i].getVector3fMap() = range_image.points[keypoint_indices.points[i]].getVector3fMap();

    //创建显示
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> keypoints_color_handler(keypoints_ptr, 255, 0, 0);
    viewer.addPointCloud<pcl::PointXYZ>(keypoints_ptr, keypoints_color_handler, "keypoints");
    viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 5, "keypoints");

    //main loop
    while (!viewer.wasStopped()) {
        range_image_widget.spinOnce();
        viewer.spinOnce();
        pcl_sleep(0.01);
    }

}