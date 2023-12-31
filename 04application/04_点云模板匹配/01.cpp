/* 20-点云模板匹配 */
#include <limits>
#include <fstream>
#include <vector>
#include <Eigen/Core>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/io/pcd_io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/filters/passthrough.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/features/normal_3d.h>
#include <pcl/features/fpfh.h>
#include <pcl/registration/ia_ransac.h>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/search/impl/search.hpp>

typedef pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> PCLHandler;
//--------------------
//定义此类目的是提供一种方便的方法来计算和储存具有每个点的局部特征描绘的点云
class FeatureCloud
{
    public:
        //A bit of shorthand
        typedef pcl::PointCloud<pcl::PointXYZ> PointCloud;
        typedef pcl::PointCloud<pcl::Normal> SurfaceNormals;
        typedef pcl::PointCloud<pcl::FPFHSignature33> LocalFeatures;//部分特色
        typedef pcl::search::KdTree<pcl::PointXYZ> SearchMethod;
        //構造函數創建一個新的:pcl:`KdTreeFLANN <pcl::KdTreeFLANN>`對象並初始化將在計算表面法線和局部特徵時使用的半徑參數。
        FeatureCloud():
                search_method_xyz_(new SearchMethod),
                normal_radius_(0.02f),
                feature_radius_(0.02f){}
        
        ~FeatureCloud(){}
/*       然後我們定義用於設置輸入雲的方法，通過將共享指針傳遞給 PointCloud 或提供要加載的 PCD 文件的名稱。
        在任何一種情況下，在設置輸入之後，都會調用processInput，它將計算局部特徵描述符，如下所述。 */
        //Process the given cloud处理给定的点云
        void
        setInputCloud(PointCloud::Ptr xyz)
        {
            xyz_ = xyz;
            processInput();
        }

        //Load and process the cloud in the given PCD file
        void
        loadInputCloud(const std::string &pcd_file)
        {
            xyz_ = PointCloud::Ptr(new PointCloud);
            pcl::io::loadPCDFile(pcd_file, *xyz_);
            processInput();
        }
/* 我們還定義了一些公共訪問器方法，可用於獲取指向點、表面法線和局部特徵描述符的共享指針。
 */ 
        //Get a pointer to the cloud 3D points
        PointCloud::Ptr
        getPointCloud() const{
            return (xyz_);
        }
       //Get a pointer to the cloud of 3D surface normals
        SurfaceNormals::Ptr
        getSurfaceNormals() const{
            return(normals_);
        }

        //Get a pointer to cloud of feature descriptors
        LocalFeatures::Ptr
        getLocalFeatures() const{
            return(features_);
        }
    protected:
        //Compute the surface normals and local features
        void
        processInput(){
            computeSurfaceNormals();
            computeLocalFeatures();
        }

        //Compute the surface normals
        void
        computeSurfaceNormals(){

            //创建表面法向量
            normals_ = SurfaceNormals::Ptr(new SurfaceNormals);

            //计算表面法向量
            pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> norm_est;
            norm_est.setInputCloud(xyz_);
            norm_est.setSearchMethod(search_method_xyz_);
            norm_est.setRadiusSearch(normal_radius_);
            norm_est.compute(*normals_);

        }
        //Compute the local feature descriptors
        //根据表面法向量，计算本地特征描述
        void
        computeLocalFeatures(){
            features_ = LocalFeatures::Ptr(new LocalFeatures);

            pcl::FPFHEstimation<pcl::PointXYZ, pcl::Normal, pcl::FPFHSignature33> fpfh_est;
            fpfh_est.setInputCloud(xyz_);
            fpfh_est.setInputNormals(normals_);
            fpfh_est.setSearchMethod(search_method_xyz_);
            fpfh_est.setRadiusSearch(feature_radius_);
            fpfh_est.compute(*features_);
        }
    private:
        //Point cloud data
        PointCloud::Ptr xyz_;
        SurfaceNormals::Ptr normals_;
        LocalFeatures::Ptr features_;//快速点特征直方图
        SearchMethod::Ptr search_method_xyz_;//KDTree方法查找领域

        //Parameters
        float normal_radius_;
        float feature_radius_;
};

//模板对齐
class TemplateAlignment{
    public:
    //A struct for storing alignment results 用于存储对齐结果的结构体
        struct Result
        {
            //匹配分数
            float fitness_score;
            //转换矩阵
            Eigen::Matrix4f final_transformation;

            EIGEN_MAKE_ALIGNED_OPERATOR_NEW
        };

        TemplateAlignment():
            min_sample_distance_(0.05f),
            max_correspondence_distance_(0.01f * 0.01f),
            nr_iterations_(500){

                //初始化SAC-IA算法的参数
                sac_ia_.setMinSampleDistance(min_sample_distance_);
                sac_ia_.setMaxCorrespondenceDistance(max_correspondence_distance_);
                sac_ia_.setMaximumIterations(nr_iterations_);
            }

        ~TemplateAlignment(){}

        //将给定的云设置为模板要对齐的目标
        void 
        setTargetCloud(FeatureCloud &target_cloud){
            target_ = target_cloud;
            //设置输入target点云
            sac_ia_.setInputTarget(target_cloud.getPointCloud());
            //设置特征target
            sac_ia_.setTargetFeatures(target_cloud.getLocalFeatures());
        }

        //将给定的云添加到模板云列表中
        void
        addTemplateCloud(FeatureCloud &template_cloud){
            templates_.push_back(template_cloud);
        }

        //将给定的模板云与setTargetCloud指定的目标对齐
        //对齐的核心代码
        void
        align(FeatureCloud &template_cloud, TemplateAlignment::Result &result){
            //设置输入原
            sac_ia_.setInputSource(template_cloud.getPointCloud());
            //设置特征元
            sac_ia_.setSourceFeatures(template_cloud.getLocalFeatures());

            pcl::PointCloud<pcl::PointXYZ> registration_output;
            sac_ia_.align(registration_output);
            //根据最远距离计算匹配分数
            result.fitness_score = (float)sac_ia_.getFitnessScore(max_correspondence_distance_);
            //获取最终转换矩阵
            result.final_transformation = sac_ia_.getFinalTransformation();
        }

        //将addTemplateCloud设置的所有模板云与setTargetCloud()指定的目标对齐
        void
        alignALL(std::vector<TemplateAlignment::Result, Eigen::aligned_allocator<Result>> &results){
            results.resize(templates_.size());
            for(size_t i = 0;i< templates_.size();++i){
                align(templates_[i], results[i]);
            }
        }

        //将所有模板云对齐到目标云，以找到对齐得分最好的一个
        int
        findBestAlignment(TemplateAlignment::Result &result){
            //Align all of the templates to the target cloud
            //将所有模板对齐到目标云
            std::vector<Result, Eigen::aligned_allocator<Result>> results;
            alignALL(results);

            //Find the template with the best fitness score
            //找到具有最佳健身分数的模板
            float lowest_score = std::numeric_limits<float>::infinity();
            int best_template  = 0;
            for(size_t i = 0; i<results.size();++i){
                const Result &r = results[i];
                if(r.fitness_score< lowest_score){
                    lowest_score = r.fitness_score;
                    best_template = (int) i;
                }
            }

            //Output the best alignment(对齐)
            result = results[best_template];
            return(best_template);

        }

    private:
        //A list of template clouds and the target to wich they will be aligned
        std::vector<FeatureCloud> templates_;
        FeatureCloud target_;

        //样本一致性初始对准(SAC-IA)配准程序及其参数
        pcl::SampleConsensusInitialAlignment<pcl::PointXYZ, pcl::PointXYZ, pcl::FPFHSignature33> sac_ia_;
        float min_sample_distance_;
        float max_correspondence_distance_;
        int nr_iterations_;
};

/**
 * 对齐对象模板集合到一个示例点云
 * 调用命令格式 ./template_alignment2 ./data/object_templates.txt ./data/person.pcd
 *                  程序                          多个模板的文本文件         目标点云
 * 调用命令格式 ./template_alignment2 ./data/object_templates2.txt ./data/target.pcd
 *                  程序                          多个模板的文本文件         目标点云
 *
 * 实时的拍照得到RGB和深度图
 * 合成目标点云图
 * 通过直通滤波框定范围（得到感兴趣区域）
 * 将感兴趣区域进行降采样（提高模板匹配效率）
 */

int main(int argc, char** argv){
    if(argc < 3){
        printf("No target PCD file giver!\n");
        return (-1);
    }

    //Load the object templates specified in the object_templates.txt file
    std::vector<FeatureCloud> object_templates;
    std::ifstream input_stream(argv[1]);//object_templates.txt
    object_templates.reserve(0);
    std::string pcd_filename;
    while(input_stream.good()){
        //按行读取模板中的文件名
        std::getline(input_stream, pcd_filename);
        if(pcd_filename.empty() || pcd_filename.at(0) == '#')
            continue;

        //注意，该对象为自定义的点云配对操作对象。
        FeatureCloud template_cloud;
        template_cloud.loadInputCloud(pcd_filename);//加载object_templates中的点云数据
        object_templates.push_back(template_cloud);//依次讲加载的点云存放到vector变量中
    }
    input_stream.close();//pcd文件加载完成，进入下一步操作

    //Load the target cloud PCD file
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::io::loadPCDFile(argv[2], *cloud);//加载模板pcd文件

    //直通滤波器,过滤z方向的点云数据
    const float depth_limit = 1.0;
    pcl::PassThrough<pcl::PointXYZ> pass;
    pass.setInputCloud(cloud);
    pass.setFilterFieldName("z");
    pass.setFilterLimits(0, depth_limit);
    pass.filter(*cloud);

    //体素滤波器,降采样
    const float voxel_grid_size = 0.005f;
    pcl::VoxelGrid<pcl::PointXYZ> vox_grid;
    vox_grid.setInputCloud(cloud);
    vox_grid.setLeafSize(voxel_grid_size, voxel_grid_size, voxel_grid_size);
    pcl::PointCloud<pcl::PointXYZ>::Ptr tempCloud(new pcl::PointCloud<pcl::PointXYZ>);
    vox_grid.filter(*tempCloud);
    cloud = tempCloud;

    //保存滤波&降采样后的点云图
    pcl::io::savePCDFileBinary("pass_through_voxel.pcd",*tempCloud);
    std::cout<<"pass_through_voxel.pcd saved"<<std::endl;

    //Assign to the target FeatureCloud 对齐到目标特征点云
    FeatureCloud target_cloud;
    target_cloud.setInputCloud(cloud);

    //Set the TemplateAlignment inputs 设置对齐输入
    TemplateAlignment template_align;
    for(size_t i=0;i < object_templates.size();i++){
        FeatureCloud &object_template = object_templates[i];
        //添加模板点云
        template_align.addTemplateCloud(object_template);
    }
    //设置目标点云
    template_align.setTargetCloud(target_cloud);

    std::cout<<"findBestAlignment"<<std::endl;
    //Find the best template alignment
    //核心代码
    TemplateAlignment::Result best_alignment;
    int best_index = template_align.findBestAlignment(best_alignment);
    const FeatureCloud &best_template = object_templates[best_index];

    //Print the alignment fitness score(values less than 0.00002 are good)
    printf("Best fitness score: %f\n", best_alignment.fitness_score);
    printf("Best fitness best_index: %d\n", best_index);

    //Print the rotation matrix and translation vector
    Eigen::Matrix3f rotation = best_alignment.final_transformation.block<3, 3>(0, 0);
    Eigen::Vector3f translation = best_alignment.final_transformation.block<3, 1>(0, 3);

    Eigen::Vector3f euler_angles = rotation.eulerAngles(2, 1, 0) * 180 /M_PI;

    printf("\n");
    printf("    | %6.3f %6.3f %6.3f | \n", rotation(0, 0), rotation(0, 1), rotation(0, 2));
    printf("R = | %6.3f %6.3f %6.3f | \n", rotation(1, 0), rotation(1, 1), rotation(1, 2));
    printf("    | %6.3f %6.3f %6.3f | \n", rotation(2, 0), rotation(2, 1), rotation(2, 2));
    printf("\n");
    cout << "yaw(z) pitch(y) roll(x) = " << euler_angles.transpose() << endl;
    printf("\n");
    printf("t = < %0.3f, %0.3f, %0.3f >\n", translation(0), translation(1), translation(2));

    //Save the aligned template for visualization
    //保存对齐的模板以便可视化
    pcl::PointCloud<pcl::PointXYZ> transformed_cloud;
    //将模板中保存的点云图进行旋转矩阵变换，把变换结果保存到transformed-cloud中
    pcl::transformPointCloud(*best_template.getPointCloud(), transformed_cloud, best_alignment.final_transformation);


    pcl::visualization::PCLVisualizer viewer("example");

    viewer.addCoordinateSystem(0.5, "cloud", 0);   //设置坐标系系统
    viewer.setBackgroundColor(0.05, 0.05, 0.05, 0);//设置背景色

    //1.旋转后的点云rotated-----------------------
    pcl::PointCloud<pcl::PointXYZ>::Ptr t_cloud(&transformed_cloud);
    PCLHandler transformed_cloud_handler(t_cloud, 255, 255, 255);
    viewer.addPointCloud(t_cloud, transformed_cloud_handler, "transformed_cloud");
    //设置渲染属性
    viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 2, "transformed_cloud");

    //2.目标点云target
    PCLHandler target_cloud_handler(cloud, 255, 100, 100);
    viewer.addPointCloud(cloud,target_cloud_handler, "target_cloud");
    //设置渲染属性
    viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "target_cloud");

    //3.模板点云template
    PCLHandler template_cloud_handler(cloud, 100, 255, 255);
    viewer.addPointCloud(best_template.getPointCloud(), template_cloud_handler, "template_cloud");
    //设置渲染属性
    viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "template_cloud");


    while(!viewer.wasStopped()){
        viewer.spinOnce();
    }

    return(0);
}
