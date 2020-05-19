#include <ros/ros.h>
#include <string.h>
#include <gazebo_msgs/LinkStates.h>
#include <math.h>
#include <time.h>

#include <moveit_visual_tools/moveit_visual_tools.h>
#include <moveit/move_group_interface/move_group_interface.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit_msgs/DisplayRobotState.h>
#include <moveit_msgs/DisplayTrajectory.h>

#include "target_pose_utils.h"
#include "obstacle_adder.h"
#include "rrtPlanner.h"
#include "bi_rrtPlanner.cpp"

using namespace std;

int getrrtType(int argc, char** argv)
{
    int rrtTypeResult = 0;
    for(int i=1;i<argc;++i){
        string paramName = argv[i];
        string paramValue = argv[++i];
        if(paramName=="rrtType"){
            if(paramValue=="0"){rrtTypeResult = 0;}
            else if(paramValue=="1"){rrtTypeResult = 1;}
            else{ROS_ERROR("Invalid rrt type, recheck your command input");}
        }
    }
    return rrtTypeResult;
}

int main(int argc, char** argv)
{    
    ros::init(argc, argv, "pos_listener");
    ros::NodeHandle nh;
    ros::AsyncSpinner spinner(1);
    spinner.start();
    ros::Rate loop_rate(30);

    // Initialize moveit relevent variables
    static const std::string PLANNING_GROUP = "manipulator";
    moveit::planning_interface::MoveGroupInterface move_group(PLANNING_GROUP);
    const robot_state::JointModelGroup* joint_model_group =
        move_group.getCurrentState()->getJointModelGroup(PLANNING_GROUP);

    robot_model_loader::RobotModelLoaderPtr robot_model_loader_;
    robot_model_loader_.reset(new robot_model_loader::RobotModelLoader("robot_description"));
    robot_model::RobotModelPtr kinematic_model = robot_model_loader_->getModel();
    planning_scene_monitor::PlanningSceneMonitorPtr planning_scene_monitor_;
    planning_scene_monitor_.reset(new planning_scene_monitor::PlanningSceneMonitor(robot_model_loader_));

    // Initialize the obstacleAdder and rrtPlanner
    Obstacle_Adder obs_adder = Obstacle_Adder(nh);
    int rrtType =  getrrtType(argc, argv);
    rrtPlanner rrt_planner = rrtPlanner(nh, kinematic_model, planning_scene_monitor_);
    rrt_planner.getParamFromCommandline(argc, argv);
    bi_rrtPlanner bi_rrt_planner = bi_rrtPlanner(nh, kinematic_model, planning_scene_monitor_);
    bi_rrt_planner.getParamFromCommandline(argc, argv);

    // Start moveit visual tools
    namespace rvt = rviz_visual_tools;
    moveit_visual_tools::MoveItVisualTools visual_tools("base_link");
    visual_tools.deleteAllMarkers();
    visual_tools.loadRemoteControl(); 
    visual_tools.trigger();

    // MAIN LOOP
    ROS_INFO("Currently running RRT planning");
    while(ros::ok()){
        // Refresh the obstacles for next plan
        obs_adder.add_obstacles();
        visual_tools.deleteAllMarkers();

        visual_tools.prompt("Press 'next' to plan a path");

        // Listen to tf broadcaster and set the transform as the move_group target
        tf::StampedTransform transform = getTargetTrans();
        tf::Quaternion q = transform.getRotation();

        // Check whether receive any transform
        if( abs(pow(q[0],2) + pow(q[1],2) + pow(q[2],2) + pow(q[3],2) - 1)<=0.01) {   
            ROS_INFO("Entering the planning mode");     
            geometry_msgs::Pose target_pose = setTarget(transform);

            ROS_INFO("The target_pose is received");
            cout << "The orientation are: " << target_pose.orientation.x << " " << target_pose.orientation.y << " " << target_pose.orientation.z << " " << target_pose.orientation.w << endl;
            cout << "The translations are: " << target_pose.position.x << " " << target_pose.position.y << " " << target_pose.position.z << endl;        

            if(rrtType==0){
                rrt_planner.setGoalNodeFromPose(target_pose);
                rrt_planner.setInitialNode(move_group.getCurrentJointValues());
            }else if(rrtType==1){
                bi_rrt_planner.setGoalNodeFromPose(target_pose);
                bi_rrt_planner.setInitialNode(move_group.getCurrentJointValues());
            }

            clock_t start, finish;
            start = clock();
            bool success;
            if(rrtType==0){success = rrt_planner.plan();}
            else if(rrtType==1){success = bi_rrt_planner.plan();}            
            finish = clock();
            double time = (double)(finish-start)/CLOCKS_PER_SEC;

            if(success){
                moveit::planning_interface::MoveGroupInterface::Plan my_plan;
                // rrt_planner.generatePlanMsg(time, &my_plan);

                cout << "Trying to get robot_state trajectory" << endl;
                ROS_INFO_NAMED("tutorial", "Visualizing plan 1 as trajectory line");
                visual_tools.publishAxisLabeled(target_pose, "pose1");
                visual_tools.publishTrajectoryLine(my_plan.trajectory_, joint_model_group);
                visual_tools.trigger();
            }else{
                cout << "After " << time << " secs of search, no available plan is found." << endl;
            }

            ROS_INFO_NAMED("Visualizing plan 1 (pose goal) %s", success ? "" : "FAILED");
        }else{
            ROS_ERROR("No target transform is received, please check the output of imgProcess.py");
        }

        ros::spinOnce();
        loop_rate.sleep();
    }

    return 0;
}
