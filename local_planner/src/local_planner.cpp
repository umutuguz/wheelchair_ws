#include <pluginlib/class_list_macros.h>
#include "local_planner/local_planner.h"

PLUGINLIB_EXPORT_CLASS(local_planner::LocalPlanner, nav_core::BaseLocalPlanner)

double x_buf[2] = {0.0, 0.0};
double y_buf[2] = {0.0, 0.0};
double xx_buf[2] = {0.0, 0.0};
double yy_buf[2] = {0.0, 0.0};
namespace local_planner
{
    LocalPlanner::LocalPlanner() : costmapROS_(NULL), tf_(NULL), initialized_(false) {}

    LocalPlanner::LocalPlanner(std::string name, tf2_ros::Buffer *tf,
                               costmap_2d::Costmap2DROS *costmapROS)
        : costmapROS_(NULL), tf_(NULL), initialized_(false), goalDistTolerance_(11)

    {
        ROS_INFO("The structure of the local planner was constructed.");
        initialize(name, tf, costmapROS);
    }

    LocalPlanner::~LocalPlanner()

    {
        ROS_INFO("Local planner out.");
    }

    // Take note that tf::TransformListener* has been changed to tf2_ros::Buffer* in ROS Noetic
    void LocalPlanner::initialize(std::string name, tf2_ros::Buffer *tf,
                                  costmap_2d::Costmap2DROS *costmapROS)
    {
        if (!initialized_)
        {
            ros::NodeHandle nh("~/local_planner");

            nh_ = nh;
            tf_ = tf;
            costmapROS_ = costmapROS;
            costmapROS_->getRobotPose(currentPoseTF2_);
            costmap_ = costmapROS_->getCostmap();

            // Subscribers
            odomSub_ = nh_.subscribe("/odom", 100, &LocalPlanner::odomCallback, this);

            scanSub_ = nh_.subscribe("/scan", 100, &LocalPlanner::scanCallback, this); //scan move base içinde remap edildi buradaki scan scan_multi_filtered a yönlendiriliyor.

            poseSub_ = nh_.subscribe("/amcl_pose", 100, &LocalPlanner::poseCallback, this);
            
            cmdSub_ = nh_.subscribe("/cmd_vel_controller", 100, &LocalPlanner::cmdCallback, this);

            // nh_.getParam("/move_base/local_planner/look_ahead_dist", lookAheadDist_);

            // Publishers
            globalPlanPub_ = nh_.advertise<nav_msgs::Path>("global_plan", 1);

            distToGoalPub_ = nh_.advertise<std_msgs::Float32>("distance_to_goal", 1);

            wRefPub_ = nh_.advertise<std_msgs::Float32>("angular_vel_output", 1);

            ROS_INFO("Local planner has been initialized successfully.");
            initialized_ = true;
        }
        else
        {
            ROS_WARN("Local planner has been initialized successfully.");
        }
    }

    bool LocalPlanner::setPlan(const std::vector<geometry_msgs::PoseStamped> &orig_global_plan)
    {
        if (!initialized_)
        {
            ROS_ERROR("This planner has not been initialized");
            return false;
        }
        globalPlan_.clear();
        globalPlan_ = orig_global_plan;
        goalReached_ = false;
        ROS_INFO("Got new plan.");

        return true;
    }

    bool LocalPlanner::computeVelocityCommands(geometry_msgs::Twist &cmd_vel)
    {
        if (!initialized_)
        {
            ROS_ERROR("This planner has not been initialized");
            return false;
        }

        ROS_INFO_ONCE("Computing velocity commands...");

        // Publish global plan for visualization
        publishGlobalPlan(globalPlan_);
        ROS_INFO("global plan size is: %lu", globalPlan_.size());
        for (int j = 0; j < globalPlan_.size(); j++)
        {
            // ROS_INFO("global plan is: %f and %f", globalPlan_[j].pose.position.x, globalPlan_[j].pose.position.y);
            // ROS_INFO("j is: %u", j);
        }

        // Find the local goal in the global plan considering look ahead distance
        for (unsigned int i = 0; i < globalPlan_.size(); i++)
        {
            currentPose_.position.x = posePtr_->pose.pose.position.x;
            currentPose_.position.y = posePtr_->pose.pose.position.y;
            currentPose_.position.z = posePtr_->pose.pose.position.z;
            // ROS_INFO("currentpose_x is: %f", currentPose_.position.x);
            // ROS_INFO("currentpose_y is: %f", currentPose_.position.y);

            double waypointX = globalPlan_[i].pose.position.x;
            double waypointY = globalPlan_[i].pose.position.y;

            double diffX = waypointX - currentPose_.position.x;
            double diffY = waypointY - currentPose_.position.y;

            double lookAheadDist_ = 30; // index
            goalDistTolerance_ = 0.25;

            // ROS_INFO("global plan waypoint index: %u", i);
            // ROS_INFO("hypot is: %f", hypot(diffX, diffY));
            // ROS_INFO("distance to global goal is: %f", distanceToGlobalGoal());
            // ROS_INFO("goaldisttolerance is: %f", goalDistTolerance_);
            // ROS_INFO("lookaheaddist is: %f", lookAheadDist_);

            if (globalPlan_.size()>lookAheadDist_)
            {
                currentGoalPoseIdx_ = lookAheadDist_;
            }
            else if (globalPlan_.size()<=lookAheadDist_ || distanceToGlobalGoal() < goalDistTolerance_)
            {
                currentGoalPoseIdx_ = globalPlan_.size()-1;
            }
            else
            {
                continue;
            }

            // if (hypot(diffX, diffY) > lookAheadDist_ && distanceToGlobalGoal() > goalDistTolerance_)
            // {
            //     currentGoalPoseIdx_ = i;
            //     break;
            // }
            // else if (distanceToGlobalGoal() < goalDistTolerance_)
            // {
            //     ROS_INFO("else if e girdik");
            //     currentGoalPoseIdx_ = globalPlan_.size() - 1;
            //     break;
            // }
            // else
            // {
            //     continue;
            // }
        }
        ROS_INFO_STREAM("Global plan size is:  " << globalPlan_.size());
        ROS_INFO_STREAM("Current goal index: " << currentGoalPoseIdx_);

        // ROS_INFO("patlamadi1");

        currentGoalPose_ = globalPlan_.at(currentGoalPoseIdx_).pose;
        // ROS_INFO("patlamadi2");
        // ROS_WARN_STREAM("Goal: " << currentGoalPose_.position.x);
        ROS_INFO_STREAM("Current goal pose: " << currentGoalPose_);

        double phiFinal = LLCallback(); // LL Algorithm

        // Print and publish the distance to global goal
        double distToGlobGoal = distanceToGlobalGoal();
        ROS_INFO_STREAM("Distance to global goal: " << distToGlobGoal);

        double angularVel;
        double linearVel;
        double dmin_temp;
        double phiFinal_temp;
        double coefVel;
        double linearVelocity;
        double alpha;
        double beta;

        alpha = 0.1535;
        beta = 0.8;

        coefVel = 1.0;

        if (dmin > 6)
        {
            dmin_temp = 6;
        }
        else if (dmin <= 0.15)
        {
            dmin_temp = 0.151; //dmin 0.15ten kücük gelirse linearvel angularvel kısımlarında sorun oluyordu.
            ROS_INFO_STREAM("dmin_temp  15 cmden kucuk mu is: " << dmin_temp);
        }
        else
        {   
            dmin_temp = dmin;
        }

        phiFinal_temp = abs(phiFinal);

        // linearVel = 0.3 * ((0.292 * log((10 * dmin_temp) + 1)) / (exp(0.883 * phiFinal_temp)) + (exp(1.57 - phiFinal_temp) / 8.01));
        // linearVel = (coefVel * ((0.7 * log((4 * (dmin_temp - 0.1)) + 0.0)) / (exp(0.883 * phiFinal_temp)) + (exp(1.57 - phiFinal_temp) / 5.0))) + 0.1;
        linearVel = (coefVel * ((0.7 * log((3.5 * (abs(dmin_temp - 0.15))) + 0.0)) / (exp(0.883 * phiFinal_temp)) + (exp(1.57 - phiFinal_temp) / 6.5))) + 0.01;
        // angularVel = phiFinal * 0.5 * (exp(dmin_temp - 10) - exp(-4 * dmin_temp) + 1);
        // angularVel = phiFinal * coefVel * (exp(dmin_temp - 10) - exp(-1 * dmin_temp) + (0.1 / (dmin_temp + 0.1)) + 1);
        angularVel = 0.75 * phiFinal * coefVel * ((exp(-4.0 * dmin_temp) / 2.0) + 1.0);

        linearVelocity = min(linearVel, cmdPtr_);
        // linearVelocity = min(10.0, cmdPtr_);
        // linearVelocity = min(linearVel, 10.0);

        ROS_INFO_STREAM("dmin_temp is: " << dmin_temp);
        ROS_INFO_STREAM("linearVel is: " << linearVel);
        ROS_INFO_STREAM("angularVel is: " << angularVel);
        ROS_INFO_STREAM("linearVelocity is: " << linearVelocity);

        if (linearVelocity <= 0.0)
        {
            linearVelocity = 0.0;
        }

        x_buf[1] = x_buf[0];
        x_buf[0] = linearVelocity;
        y_buf[1] = y_buf[0];

        y_buf[0] = y_buf[1] * (1 - alpha) + alpha * x_buf[0];
        linearVelocity = y_buf[0]; //linear hız denklemden gelen alınır önce, sonra bir önceki cycledaki linear hız ile alpha, 1-alpha oranında birleştirilir.

        xx_buf[1] = xx_buf[0];
        xx_buf[0] = angularVel;
        yy_buf[1] = yy_buf[0];

        yy_buf[0] = yy_buf[1] * (1 - beta) + beta * xx_buf[0]; //aynısı angular hız için beta kullanılarak yapılır.
        angularVel = yy_buf[0];

        // ROS_INFO_STREAM("Lineer velocity: " << linearVelocity);
        // ROS_INFO_STREAM("Angular velocity: " << angularVel);
        // ROS_INFO_STREAM("dmin: " << dmin_temp);

        // Send velocity commands to robot's base
        cmd_vel.linear.x = linearVelocity;
        cmd_vel.linear.y = 0.0;
        cmd_vel.linear.z = 0.0;

        cmd_vel.angular.x = 0.0;
        cmd_vel.angular.y = 0.0;
        cmd_vel.angular.z = angularVel;

        if (distanceToGlobalGoal() < goalDistTolerance_)
        {
            cmd_vel.linear.x = 0.0;
            cmd_vel.linear.y = 0.0;
            cmd_vel.linear.z = 0.0;

            cmd_vel.angular.x = 0.0;
            cmd_vel.angular.y = 0.0;
            cmd_vel.angular.z = 0.0;

            goalReached_ = true;
        }

        if (!isGapExist_)
        {
            // ROS_INFO("Gap yok!");

            if (!isGoalReached())
            {
                if ((distanceToGlobalGoal() > 0.25) && (dmin > 0.15))
                {
                    // Use the last refence cmd_vel command
                    cmd_vel.linear.x = linearVelocity;
                    cmd_vel.linear.y = 0.0;
                    cmd_vel.linear.z = 0.0;

                    cmd_vel.angular.x = 0.0;
                    cmd_vel.angular.y = 0.0;
                    cmd_vel.angular.z = angularVel;
                    ROS_INFO("Gap yok, globale gidiyor.");
                }
                else
                {
                ROS_INFO("Gap yok, globale ulasilamadi.");
                cmd_vel.linear.x = 0.0;
                cmd_vel.linear.y = 0.0;
                cmd_vel.linear.z = 0.0;

                cmd_vel.angular.x = 0.0;
                cmd_vel.angular.y = 0.0;
                cmd_vel.angular.z =-0.5; //çözümsüz kaldığı durumlarda kendi etrafında dönsün diye 
                }
            }
            
        }
        return true;
    }

    bool LocalPlanner::isGoalReached()
    {
        if (!initialized_)
        {
            ROS_ERROR("This planner has not been initialized");
            return false;
        }

        if (!costmapROS_->getRobotPose(currentPoseTF2_))
        {
            ROS_ERROR("Could not get robot pose");
            return false;
        }

        if (goalReached_)
        {
            ROS_INFO("Goal reached!");
            return true;
        }

        return false;
    }

    void LocalPlanner::odomCallback(boost::shared_ptr<nav_msgs::Odometry const> msg)
    {
        odomPtr_ = msg;
    } // end function odomCallback

    void LocalPlanner::scanCallback(boost::shared_ptr<sensor_msgs::LaserScan const> msg)
    {
        scanPtr_ = msg;
        } // end function laserScanCallback

    void LocalPlanner::poseCallback(boost::shared_ptr<geometry_msgs::PoseWithCovarianceStamped const> msg)
    {
        posePtr_ = msg;
    } // end function poseCallback
    
    void LocalPlanner::cmdCallback(const std_msgs::Float64::ConstPtr& msg)
    {
        cmdPtr_ = msg->data;
    } // end function cmdCallback

    double LocalPlanner::distanceToGlobalGoal()
    {
        currentPose_.position.x = posePtr_->pose.pose.position.x;
        currentPose_.position.y = posePtr_->pose.pose.position.y;
        currentPose_.position.z = posePtr_->pose.pose.position.z;

        geometry_msgs::PoseStamped globalGoal = globalPlan_.back();
        double xDist = globalGoal.pose.position.x - currentPose_.position.x;
        double yDist = globalGoal.pose.position.y - currentPose_.position.y;

        return hypot(xDist, yDist);
    }

    double LocalPlanner::LLCallback()
    {
        ROS_INFO_STREAM("Local Planner started");
        lastCallbackTime_ = ros::Time::now().toSec();

        // Get odometry informations
        // WARNING: These are not odometry information! Variable names remained
        // unchanged since the latest update. These are AMCL positions.
        double odomRX = posePtr_->pose.pose.position.x;
        double odomRY = posePtr_->pose.pose.position.y;
        currentPose_.orientation = posePtr_->pose.pose.orientation;
        double robot_pose_theta = tf::getYaw(currentPose_.orientation);
        robot_pose_theta = robot_pose_theta * 180 / M_PI;
        // ROS_INFO("robot_pose_theta real is: %f", robot_pose_theta);


        double goalX = currentGoalPose_.position.x;
        double goalY = currentGoalPose_.position.y;


        // Get laser ranges
        std::vector<double> laserRanges;
        std::vector<double> currRange;
        ROS_INFO_STREAM("Scan ptr size is: " << scanPtr_->ranges.size());

        for (unsigned int i = 0; i < scanPtr_->ranges.size(); i++)
        {
            laserRanges.push_back(scanPtr_->ranges[i]);
            // ROS_INFO_STREAM("this is laserRanges vector, at index: "<< i << " range is: " << laserRanges[i]);
        }

        currRange = laserRanges;
        // currRange.erase(currRange.begin(), currRange.begin() + 190); //scanmulti filtrelenmiş verisi sandalyenin arkası 0 olacak şekilde saat yönü tersinde geliyor
        // buradan arkadan ilk 90 ve son 90 derece kırpılarak field of view sadece öndeki 180 derece olacak şekilde ayarlanmıştır.
        ROS_INFO_STREAM("currrange size is now: "<< currRange.size());
        // currRange.erase(currRange.begin() + 380, currRange.end());
        // ROS_INFO_STREAM("currrange size is now: "<< currRange.size());

        // ilk 90 veri soldan karşıya olan tarama, ikinci 90 veri
        // karşıdan sağa olan tarama olacak şekilde ayarlandı. Turtlebot3 lidarı dönüş yönünden ötürü böyle. Sandalyeye geçildiğinde tarama yönünün teyit edilmesi gerekli.

        reverse(currRange.begin(), currRange.end()); // kırpma işleminden sonra sağdan sola taranmış şekilde öndeki 180 derecelik veriler kalmıştı
        // ters çevrilerek ilk indeksli olan nokta sol 90derecede kalan yer olmaktadır buradan sağa doğru taranmış hale gelir.

        // her lazer ölçümünden 25cm çıkartıldı (obstacle inflation)
        for (unsigned int i = 0; i < currRange.size() ; i++)
        {
            currRange[i] -= 0.25;
        }

        // auto dminIdxItr = std::min_element(currRange.begin(), currRange.end());
        auto dminIdxItr = std::min_element(currRange.begin()+75, currRange.end()-75);
        // int dminIdx = std::distance(currRange.begin(), dminIdxItr);
        int dminIdx = std::distance(currRange.begin()+75, dminIdxItr);

        // dmin = currRange.at(dminIdx);
        dmin = currRange.at(dminIdx+75);

        if (dmin <= 0.01)
        {
            dmin = 0.01;
        }
        ROS_INFO_STREAM("dmin is " << dmin);
        // for (unsigned int i = 0; i < currRange.size(); i++)
        // {
        //     ROS_INFO_STREAM("currrange vector is: "<< currRange[i] << "for index : " << i);
        // }

        std::vector<int> gap_starting_points;
        std::vector<int> gap_ending_points;
        std::vector<int> common_angles;
        int gap_number;
        int gap_validator;

        for (unsigned int i = 0; i < currRange.size() - 1; i++)
        {
            if (currRange[i + 1] > currRange[i] + 1.0)
            {
                gap_number = gap_number + 1;
                gap_starting_points.push_back(i);
            }
            if (currRange[i] > currRange[i + 1] + 1.0)
            {
                gap_validator = gap_validator + 1;
                gap_ending_points.push_back(i + 1);
            }
        }
        double phiGoal;
        double phiFinal;
        double xDiff;
        double yDiff;
        
        xDiff = goalX - odomRX;
        yDiff = goalY - odomRY;

        if ((90 < robot_pose_theta) && (robot_pose_theta < 180))
            robot_pose_theta = 450 - robot_pose_theta;
        else
            robot_pose_theta = 90 - robot_pose_theta;
        phiGoal = atan2(yDiff, xDiff);
        phiGoal = phiGoal * 180 / M_PI;
        // ROS_INFO_STREAM("Goal angle 1 is: " << phiGoal);

        // if ((odomRX > goalX) && (odomRY < goalY))
        if ((odomRX >= goalX) && (odomRY <= goalY)) // Belki çözer?
            phiGoal = 450 - phiGoal;
        else
            phiGoal = 90 - phiGoal;

        if (goalX == odomRX && goalY > odomRY)
        {
            phiGoal = 0;
        }

        if (goalX == odomRX && goalY < odomRY)
        {
            phiGoal = 180;
        }

        if (goalX > odomRX && goalY == odomRY)
        {
            phiGoal = 90;
        }

        if (goalX < odomRX && goalY == odomRY)
        {
            phiGoal = 270;
        }
        // ROS_INFO_STREAM("robot pose theta : " << robot_pose_theta);
        // ROS_INFO_STREAM("phi goal is: " << phiGoal);

        // phiGoal = phiGoal + robot_pose_theta;

        ROS_INFO_STREAM("hic manupule edilmemis phi goal: " << phiGoal);

        phiGoal = phiGoal - (robot_pose_theta - 90);

        ROS_INFO_STREAM("manupule edilmemis phi goal: " << phiGoal);

        if (phiGoal < 0.0)
        {
            phiGoal = phiGoal + 360.0;
        }
        else if (phiGoal > 270.0)
        {
            // phiGoal = 450.0 - phiGoal;
            phiGoal = phiGoal - 360.0;
        }

        ROS_INFO_STREAM("Onemli phi goal: " << phiGoal);
        ROS_INFO_STREAM("Onemli robot pose theta: " << robot_pose_theta);

        if (gap_starting_points.size()== 0 || gap_ending_points.size()==0)
        {
            isGapExist_ = false;
            if (phiGoal > 270)
                phiFinal = (450 - phiGoal) * (M_PI / 180);
            else
                phiFinal = (90 - phiGoal) * (M_PI / 180);

            if (phiFinal > M_PI && phiFinal < 2*M_PI)
            {
                phiFinal = phiFinal - 2*M_PI;
            }
            else if (phiFinal > 2*M_PI)
            {
                phiFinal = M_PI_2 - (phiFinal - 2*M_PI);
            }
            return phiFinal;
        }
        else
        {
            isGapExist_ = true;
        }

        // for (int i = 0; i < gap_starting_points.size(); i++)
        // {
        //     ROS_INFO_STREAM("gap starting points are: " << gap_starting_points[i]);
        // }
        // for (int i = 0; i < gap_ending_points.size(); i++)
        // {
        //     ROS_INFO_STREAM("gap ending points are: " << gap_ending_points[i]);
        // }

        // gap starting ve gap ending vektörlerinde ortak olan açı değerlerini bulma kısmı (MATLAB intersect fonksiyonunun, common kısmı)
        std::vector<int> intersector(gap_starting_points.size() + gap_ending_points.size());
        std::vector<int>::iterator it, end;

        end = set_intersection(gap_starting_points.begin(), gap_starting_points.end(), gap_ending_points.begin(), gap_ending_points.end(), intersector.begin());

        for (it = intersector.begin(); it != end; it++)
        {
            common_angles.push_back(*it);
        }

        // for (int i = 0; i < common_angles.size(); i++)
        // {
        //     ROS_INFO_STREAM("common angles are: " << common_angles[i]);
        // }

        // MATLAB intersect fonksiyonunun, her iki vektörde ortak olan elemanların indexlerini belirlediği kısım
        for (auto x : common_angles)
        {
            int key = x;
            vector<int>::iterator itr_start = std::find(gap_starting_points.begin(), gap_starting_points.end(), key);
            vector<int>::iterator itr_end = std::find(gap_ending_points.begin(), gap_ending_points.end(), key);

            if (itr_start != gap_starting_points.end())
            {
                int index_of_common_angle = std::distance(gap_starting_points.begin(), itr_start);
                // ROS_INFO_STREAM("starting points vector indices are: " << index_of_common_angle);
                gap_starting_points.erase(gap_starting_points.begin() + index_of_common_angle);
            }

            if (itr_end != gap_ending_points.end())
            {
                int index_of_common_angle = std::distance(gap_ending_points.begin(), itr_end);
                // ROS_INFO_STREAM("ending points vector indices are: " << std::distance(gap_ending_points.begin(), itr_end));
                gap_ending_points.erase(gap_ending_points.begin() + index_of_common_angle);
            }
        }

        // for (int i = 0; i < gap_starting_points.size(); i++)
        // {
        //     ROS_INFO_STREAM("gap starting points are: " << gap_starting_points[i]);
        // }
        // for (int i = 0; i < gap_ending_points.size(); i++)
        // {
        //     ROS_INFO_STREAM("gap ending points are: " << gap_ending_points[i]);
        // }

        int counter = 0;
        int j = 0;
        int k = 1;
        int l = 0;
        int m = 1;
        int n = 0;
        int o = 1;
        vector<int> indices;

        for (unsigned int i = 0; i < gap_starting_points.size(); i++)
        {
            if (i == 0)
            {
                double temp = gap_starting_points[i];
                // ROS_INFO_STREAM("temp is : " << temp);
                for (j; j < gap_ending_points.size(); j++)
                {
                    if (gap_ending_points[j] < temp)
                    {
                        counter++;
                        // ROS_INFO_STREAM("counter is : " << counter);
                        indices.push_back(j);
                        // ROS_INFO_STREAM("indices are : " << indices[j]);
                    }
                }
                if (counter > 1)
                {
                    for (k; k < indices.size(); k++)
                    {
                        // ROS_INFO_STREAM(" k is " << k);
                        // ROS_INFO_STREAM("indices size is : " << indices.size());
                        gap_ending_points.erase(gap_ending_points.begin() + 1);
                        gap_ending_points.insert(gap_ending_points.begin() + 1, -99);
                    }
                }
                // ROS_INFO_STREAM("gap ending points are : ");
                // for (unsigned int z = 0; z < gap_ending_points.size(); z++)
                // {
                //     ROS_INFO_STREAM(" " << gap_ending_points[z]);
                // }

                // ROS_INFO_STREAM("gap starting points are : ");
                // for (unsigned int z = 0; z < gap_starting_points.size(); z++)
                // {
                //     ROS_INFO_STREAM(" " << gap_starting_points[z]);
                // }
            }
            indices = {};
            counter = 0;

            if (i > 0)
            {
                double temp_prev = gap_starting_points[i - 1];
                double temp = gap_starting_points[i];
                // ROS_INFO_STREAM("temp prev is : " << temp_prev);
                // ROS_INFO_STREAM("temp is : " << temp);
                for (l; l < gap_ending_points.size(); l++)
                {
                    // ROS_INFO_STREAM("inside l = " << l);
                    if ((gap_ending_points[l] < temp) && (gap_ending_points[l] > temp_prev))
                    {
                        counter++;
                        // ROS_INFO_STREAM("counter is : " << counter);
                        indices.push_back(l);
                    }
                }

                if (counter > 1)
                {
                    for (m; m < indices.size(); m++)
                    {
                        // ROS_INFO_STREAM(" m is " << m);
                        // ROS_INFO_STREAM("indices size is : " << indices.size());
                        gap_ending_points.erase(gap_ending_points.begin() + indices[1]);
                        gap_ending_points.insert(gap_ending_points.begin() + indices[1], -99);
                    }
                    m = 1;
                }

                if (counter == 0)
                {
                    gap_starting_points.erase(gap_starting_points.begin() + i - 1);
                    gap_starting_points.insert(gap_starting_points.begin() + i - 1, -99);
                }
                counter = 0;
                // ROS_INFO_STREAM("indices are: ");
                // for (unsigned int z = 0; z < indices.size(); z++)
                // {
                //     ROS_INFO_STREAM(" " << indices[z]);
                // }
                // ROS_INFO_STREAM("gap ending points are : ");
                // for (unsigned int z = 0; z < gap_ending_points.size(); z
                //     ROS_INFO_STREAM(" " << gap_ending_points[z]);
                // }

                // ROS_INFO_STREAM("gap starting points are : ");
                // for (unsigned int z = 0; z < gap_starting_points.size(); z++)
                // {
                //     ROS_INFO_STREAM(" " << gap_starting_points[z]);
                // }
            }
            indices = {};
            l = 0;

            if (i == gap_starting_points.size() - 1)
            {
                double temp = gap_starting_points[i];
                // ROS_INFO_STREAM("temp is : " << temp);
                for (n; n < gap_ending_points.size(); n++)
                {
                    if (gap_ending_points[n] > temp)
                    {
                        indices.push_back(n);
                        // ROS_INFO_STREAM("indices are : " << indices[counter]);
                        counter++;
                        // ROS_INFO_STREAM("counter is : " << counter);
                    }
                }
                if (counter > 1)
                {
                    for (o; o < indices.size(); o++)
                    {
                        // ROS_INFO_STREAM(" o is " << o);
                        // ROS_INFO_STREAM("indices size is : " << indices.size());
                        gap_ending_points.erase(gap_ending_points.begin() + indices[1]);
                        gap_ending_points.insert(gap_ending_points.begin() + indices[1], -99);
                    }
                }
                if (counter == 0)
                {
                    gap_ending_points.push_back(344);
                }
                // ROS_INFO_STREAM("gap ending points are : ");
                // for (unsigned int z = 0; z < gap_ending_points.size(); z++)
                // {
                //     ROS_INFO_STREAM(" " << gap_ending_points[z]);
                // }

                // ROS_INFO_STREAM("gap starting points are : ");
                // for (unsigned int z = 0; z < gap_starting_points.size(); z++)
                // {
                //     ROS_INFO_STREAM(" " << gap_starting_points[z]);
                // }
            }
            counter = 0;
            j = 0;
            k = 1;
            l = 0;
            m = 1;
            n = 0;
            o = 1;
            indices = {};
        }

        // asagidaki donguler iki tane -99 olan eleman varsa patlıyor. i=0da ve i=1 de varsa önce i=0dakini siliyor, i=1deki sıfıra geçtiği için tekrar oraya bakmadan devam ediyor.

        for (unsigned int i = 0; i < gap_starting_points.size(); i++)
        {
            if (gap_starting_points[i] == -99)
            {
                gap_starting_points.erase(gap_starting_points.begin() + i);
                i--;
            }
        }
        for (unsigned int i = 0; i < gap_ending_points.size(); i++)
        {
            if (gap_ending_points[i] == -99)
            {
                gap_ending_points.erase(gap_ending_points.begin() + i);
                i--;
            }
        }

        gap_starting_points.erase(remove(gap_starting_points.begin(), gap_starting_points.end(), -99), gap_starting_points.end());

        if (gap_ending_points[0] < gap_starting_points[0])
        {
            gap_starting_points.insert(gap_starting_points.begin(), 0);
        }
        // ROS_INFO_STREAM("gap ending points are : ");
        // for (unsigned int z = 0; z < gap_ending_points.size(); z++)
        // {
        //     ROS_INFO_STREAM(" " << gap_ending_points[z]);
        // }

        // ROS_INFO_STREAM("gap starting points are : ");
        // for (unsigned int z = 0; z < gap_starting_points.size(); z++)
        // {
        //     ROS_INFO_STREAM(" " << gap_starting_points[z]);
        // }

        if (!common_angles.empty())
        {
            for (int i = 0; i < common_angles.size(); i++)
            {
                gap_starting_points.push_back(common_angles[i]);
                gap_ending_points.push_back(common_angles[i]);
            }
        }

        sort(gap_starting_points.begin(), gap_starting_points.end());
        sort(gap_ending_points.begin(), gap_ending_points.end());

        int min_size;

        // ROS_INFO_STREAM("gap starting points are : ");
        // for (int i = 0; i < gap_starting_points.size(); i++)
        // {
        //     ROS_INFO_STREAM(gap_starting_points[i]);
        // }
        // ROS_INFO_STREAM("gap ending points are : ");
        // for (int i = 0; i < gap_ending_points.size(); i++)
        // {
        //     ROS_INFO_STREAM(gap_ending_points[i]);
        // }

        min_size = min(gap_starting_points.size(), gap_ending_points.size());

        double array_gap[min_size][2];

        for (int i = 0; i < min_size; i++)
        {
            for (int j = 0; j < 2; j++)
            {
                if (j == 0)
                {
                    array_gap[i][j] = gap_starting_points[i];
                }
                else
                {
                    array_gap[i][j] = gap_ending_points[i];
                }
            }
        }
        // ROS_INFO_STREAM("min_size is = " << min_size);
        int counter_array = 0;

        // for (int i = 0; i < min_size; i++)
        // {
        //     for (int j = 0; j < 2; j++)
        //     {
        //         counter_array++;
        //         ROS_INFO_STREAM("Array gap's " << counter_array << " element is = " << array_gap[i][j]);
        //     }
        // }

        common_angles.erase(common_angles.begin(), common_angles.end());

        int rows, cols;
        int idx = 0;
        int max_gap = 0;
        double alpha = 0.0;
        double beta = 0.0;

        double d1 = 0.0, d2 = 0.0;
        double phi_gap = 0.0;

        rows = sizeof(array_gap) / sizeof(array_gap[0]);
        cols = sizeof(array_gap[0]) / sizeof(array_gap[0][0]);

        for (int i = 0; i < rows; i ++)
        {
            for (int j = 0 ; j < cols ; j++)
            {
                array_gap[i][j] = array_gap[i][j] * (163.0/344.0);
            }
        }
        counter_array = 0;
        for (int i = 0; i < min_size; i++)
        {
            for (int j = 0; j < 2; j++)
            {  
                counter_array++;
                ROS_INFO_STREAM("Array gap's " << counter_array << " element is = " << array_gap[i][j]);
                ROS_INFO_STREAM("Array gap's rounded " << counter_array << " element is = " << round(array_gap[i][j]));
            }
        }

        double memory_array[min_size][4];

        double lidar_coord_x;
        double lidar_coord_y;

        lidar_coord_x = odomRX + 0.722*sin(robot_pose_theta*(M_PI/180.0));
        lidar_coord_y = odomRY + 0.722*cos(robot_pose_theta*(M_PI/180.0));

        // ROS_INFO_STREAM("lidar coord x is: "<< lidar_coord_x);
        // ROS_INFO_STREAM("lidar coord y is: "<< lidar_coord_y);
        // ROS_INFO_STREAM("odomrx is: "<< odomRX);
        // ROS_INFO_STREAM("odomry is: "<< odomRY);






        // Gap odullendirme baslangici

        
        vector<double> gap_midpoints;
        vector <double> diff_to_goal;
        double d1_temp, d2_temp, alpha_temp, beta_temp, midpoint;

        for (int i = 0; i < rows; i++) // her gap icin midpoint makaledeki denklemle hesaplanır
        {
            // ROS_INFO_STREAM("Total gap count is: " << i+1);
            alpha_temp = array_gap[i][0];
            beta_temp = array_gap[i][1];
            // ROS_INFO_STREAM("d1_temp at: " << alpha_temp*(344.0/163.0));
            ROS_INFO_STREAM("alpha_temp at: " << alpha_temp);
            d1_temp = currRange.at(round(alpha_temp*(344.0/163.0)));
            // ROS_INFO_STREAM("d2_temp at: " << beta_temp*(344.0/163.0));
            ROS_INFO_STREAM("beta_temp at: " << beta_temp);
            if (beta_temp >= 163.0)
                beta_temp = 162.01;
                ROS_INFO_STREAM("beta_temp at: " << beta_temp);
            d2_temp = currRange.at(round(beta_temp*(344.0/163.0)));
            if (alpha_temp == 0)
            {
                d1_temp = d2_temp;
            }
            if (beta_temp == 162.01)
            {
                d2_temp = d1_temp;
            }
            memory_array[i][0] = lidar_coord_x - d1_temp*cos(M_PI*(robot_pose_theta + (alpha_temp+8.5))/180.0);
            memory_array[i][1] = lidar_coord_y + d1_temp*sin(M_PI*(robot_pose_theta + (alpha_temp+8.5))/180.0);
            memory_array[i][2] = lidar_coord_x - d2_temp*cos(M_PI*(robot_pose_theta + (beta_temp+8.5))/180.0);
            memory_array[i][3] = lidar_coord_y + d2_temp*sin(M_PI*(robot_pose_theta + (beta_temp+8.5))/180.0);
            // ROS_INFO_STREAM("d1X is : " << memory_array[i][0]);
            // ROS_INFO_STREAM("d1Y is : " << memory_array[i][1]);
            // ROS_INFO_STREAM("d2X is : " << memory_array[i][2]);
            // ROS_INFO_STREAM("d2Y is : " << memory_array[i][3]);


            midpoint = 180*(acos((d1_temp + d2_temp * cos((M_PI / 180) * (beta_temp + 8.5) - (M_PI / 180) * (alpha_temp + 8.5))) / sqrt(d1_temp * d1_temp + d2_temp * d2_temp + 2 * d1_temp * d2_temp * cos((M_PI / 180) * (beta_temp + 8.5) - (M_PI / 180) * (alpha_temp + 8.5)))) + (M_PI / 180) * (alpha_temp + 8.5))/M_PI;
            gap_midpoints.push_back(midpoint);
            diff_to_goal.push_back(fabs(midpoint - phiGoal));
            // ROS_INFO_STREAM("d1 temp is : " << d1_temp);
            // ROS_INFO_STREAM("d2 temp is : " << d2_temp);
        }

        ROS_INFO_STREAM("Gap count is: " << gap_midpoints.size());

        if (gap_midpoints.size() != 0)
        {
            for (int i = 0; i<gap_midpoints.size();i++)
            {
                ROS_INFO_STREAM("Gap mid point is: " << gap_midpoints[i]);
            }
        }

        vector<double> gap_sizes;
        // double gap_weight = 0.1;
        double gap_slew_rate = 0.5; //Gap büyütme hızını belirler, bu katsayıyı büyütmek hızı üstel olarak büyütür.
        double gap_expansion = 0.5; //Gap büyütme oranını belirler, bu katsayıyı büyütmek gap genişleme oranını doğrusal arttırır.

        for (int i = 0; i < rows; i++) //midpointlerin 
        {
            gap_sizes.push_back(fabs(array_gap[i][1] - array_gap[i][0]));
            // ROS_INFO_STREAM("Real gap size is: " << gap_sizes[i]);
            // gap_sizes[i] = gap_sizes[i] + gap_sizes[i] * sqrt(pow((1 / diff_to_goal[i]),gap_weight));
            gap_sizes[i] = gap_sizes[i] + gap_sizes[i] * (exp(-gap_slew_rate * (M_PI / 180.0) * diff_to_goal[i]) * gap_expansion);
            // ROS_INFO_STREAM("Rewarded gap size is: " << gap_sizes[i]);
        }

        // gap odullendirme bitisi


        int max_gap_idx = max_element(gap_sizes.begin(), gap_sizes.end()) - gap_sizes.begin();
        ROS_INFO_STREAM("max gap indx is: "<< max_gap_idx);

        int min_gap_idx = min_element(gap_sizes.begin(), gap_sizes.end()) - gap_sizes.begin();
        // ROS_INFO_STREAM("min gap indx is: "<< min_gap_idx);

        alpha = array_gap[max_gap_idx][0];
        beta = array_gap[max_gap_idx][1];
        if (beta >= 163)
                beta = 162.01;

        ROS_INFO_STREAM("alpha is: " << (alpha+8.5));
        ROS_INFO_STREAM("beta is: " << (beta+8.5));

        if (alpha != 0.0)
        {
            d1 = currRange.at(round(alpha*(344.0/163.0)));
        }
        ROS_INFO_STREAM("d1 1: " << d1);

        if (beta != 180.0)
        {   
            // ROS_INFO_STREAM("d2 11: " << d2);
            d2 = currRange.at(round(beta*(344.0/163.0)));
        }

        ROS_INFO_STREAM("d2 1: " << d2);

        if (alpha == 0.0)
        {
            d1 = currRange.at(round(beta*(344.0/163.0)));
        }

        ROS_INFO_STREAM("d1 2: " << d1);

        if (beta == 180.0)
        {   
            // ROS_INFO_STREAM("d2 2: " << d2);
            d2 = currRange.at(round(alpha*(344.0/163.0)));
        }
        ROS_INFO_STREAM("d2 2: " << d2);

        if (alpha == 0.0)
        {
            d1 = d2;
        }
        if (beta == 162.01)
        {
            d2 = d1;
        }

        // ROS_INFO_STREAM("alpha is: " << alpha << "beta is : " << beta << "sqrt term is : " << d1 * d1 + d2 * d2 + 2 * d1 * d2 * cos((M_PI / 180) * beta - (M_PI / 180) * alpha));

        phi_gap = acos((d1 + d2 * cos((M_PI / 180) * (beta + 8.5) - (M_PI / 180) * (alpha + 8.5))) / sqrt(d1 * d1 + d2 * d2 + 2 * d1 * d2 * cos((M_PI / 180) * (beta + 8.5) - (M_PI / 180) * (alpha + 8.5)))) + (M_PI / 180) * (alpha + 8.5);
        phi_gap = phi_gap * 180 / M_PI;

        // phi_gap = ((180.0/M_PI) * acos((d1 + d2 * cos(M_PI/180.0*(beta-alpha))) / sqrt(pow(d1, 2) + pow(d2, 2) + 2*d1*d2*cos(M_PI/180.0*(beta-alpha))))) + alpha;

        // ROS_INFO_STREAM("phi gap is : " << phi_gap);
        // ROS_INFO_STREAM("d1 is : " << d1);
        // ROS_INFO_STREAM("d2 is : " << d2);

        // ROS_INFO_STREAM("odomRX is : " << odomRX);
        // ROS_INFO_STREAM("odomRY is : " << odomRY);
        // ROS_INFO_STREAM("goalX is : " << goalX);
        // ROS_INFO_STREAM("goalY is : " << goalY);

        // ROS_INFO_STREAM("Goal angle is: " << phiGoal);
        // ROS_INFO_STREAM("robot_pose_theta is : " << robot_pose_theta);

        // ROS_WARN_STREAM("Gap existance: " << isGapExist_);
        // ROS_WARN_STREAM("Phi final: " << phiFinal);

        double alpha_weight = 0.5;
        //double beta_weight = 2.8;
        phiFinal = (((alpha_weight / exp(dmin)) * (phi_gap * M_PI/180)) + (phiGoal * M_PI/180)) / (alpha_weight / exp(dmin) + 1);
        // phiFinal = phi_gap;
        // ROS_INFO_STREAM("moving to : "<< phiFinal);
        //double phiFinal = 0; //(90-phiGoal)*M_PI/180;

        if (phiFinal > 1.5*M_PI)
            phiFinal = (2.5*M_PI - phiFinal);
        else
            phiFinal = (M_PI_2 - phiFinal);

        if (phiFinal > M_PI && phiFinal < 2*M_PI)
        {
            phiFinal = phiFinal - 2*M_PI;
        }
        else if (phiFinal > 2*M_PI)
        {
            phiFinal = M_PI_2 - (phiFinal - 2*M_PI);
        }

        // ROS_INFO_STREAM("alpha_weight/dmin is: " << alpha_weight/dmin);
        ROS_INFO_STREAM("phi gap is : " << phi_gap);
        ROS_INFO_STREAM("phi goal is : " << phiGoal);
        double moving_to;
        moving_to = 90 - phiFinal*180/M_PI;
        ROS_INFO_STREAM("moving to : " << moving_to);

        return phiFinal;
    }

    void LocalPlanner::publishGlobalPlan(std::vector<geometry_msgs::PoseStamped> &path)
    {
        base_local_planner::publishPlan(path, globalPlanPub_);
    } // end function publishGlobalPlan

    // void publishDistToGoal(const ros::Publisher &pub, double dist)
    // {
    //     std_msgs::Float32 msg;
    //     msg.data = dist;

    //     pub.publish(msg);
    // } // end function publishDistToGoal

    void publishWRef(const ros::Publisher &pub, double wRef)
    {
        std_msgs::Float32 msg;
        msg.data = wRef;

        pub.publish(msg);
    } // end function publishWRef
}