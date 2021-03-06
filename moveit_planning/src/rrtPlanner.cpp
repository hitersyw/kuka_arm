#include "rrtPlanner.h"
#include <iostream>
#include <math.h>
#include <random>

using namespace std;

rrtPlanner::rrtPlanner(ros::NodeHandle& nh,
                       robot_model::RobotModelPtr kinematic_model,
                       planning_scene_monitor::PlanningSceneMonitorPtr planning_scene_monitor_) : nh(nh)
{
    this->kinematicModel = kinematic_model;
    robot_state::RobotStatePtr kinematic_state(new robot_state::RobotState(kinematic_model));
    this->kinematicState = kinematic_state;
    this->kinematicState->setToDefaultValues();
    this->jointModelGroup = kinematic_model->getJointModelGroup("manipulator");
    this->planningSceneMonitor_ = planning_scene_monitor_;

    // Rviz display publisher
    markerPub = nh.advertise<visualization_msgs::Marker>("visualization_marker", 10);
    // Readin joint limits
    readJntLimits();           
}

bool rrtPlanner::plan()
{
    // Set some parameters i.e. this->success...
    initialize();
    rrtTree.push_back(initialNode);

    // The main plan process, return true if successfully planned.
    int count = 0;
    while(!this->success && count < this->maxIter){
        node randNode = sampleNode();
        int nearestNodeid = findNearest(randNode, this->rrtTree);
        node newNode = extend(nearestNodeid, randNode);
        if(newNode.id!=-1){ // -1 means that the newNode is not feasible
            if( checkReachGoal(newNode) ){
                this->success = true;
            }
            rrtTree.push_back(newNode);

            if(this->enableVisual != VISUAL_TYPES::NO_VISUAL){
                drawNewNode(newNode);
            }
        }
        cout << "Current node count: " << rrtTree.size() << endl;
        count += 1;
    }
    
    if(this->success){
        findPath();
        drawPlan();
    }else{
        ROS_WARN("No available path found");
    }
    cout << "Minimum distance is: " << minGoalDist << endl;
    cout << "Maximum distance is: " << maxGoalDist << endl;
    return this->success;
}


node rrtPlanner::sampleNode()
{
    // Randomly sample a node in the configuration space
    int jointAng;
    node randNode;
    randNode.prevNodeid = -1;
    srand(time(NULL));
    random_device rd;

    // if goalExtend, then 5 percent probability that the randNode is set to the goalNode
    if(this->goalExtend && rd()%100 < 10){
        // Sample a goal node in goalNodes. Currently only one node, therefore abandoned
        // int goalId = rd() % this->goalNodes.size();
        // randNode = goalNodes[goalId]; 
        randNode = this->goalNodes[0];
    }else{
        for(int i=0; i<JOINTNUM; i++){
            jointAng = rd() % (jointUpperLimits[i]-jointLowerLimits[i]) + jointLowerLimits[i];
            randNode.jointAngles.push_back(jointAng);
        }
    }
    
    return randNode;
}

int rrtPlanner::findNearest(node randNode, vector<node> tree)
{
    // Use brute-force method to find the nearest node among the tree
    // return the id of that nearest node
    double dist, minDist = DBL_MAX;
    int minid = -1;
    for(int i=0;i<tree.size();i++){
        dist = calcDist(tree[i], randNode); 
        if(dist<minDist){
            minDist = dist;
            minid = i;
        }
    }
    return minid;
}

double rrtPlanner::calcDist(node a, node b)
{
    double result, distance = 0;
    double angleDist = 0;
    for(int i=0;i<JOINTNUM;i++){
        // NOTE that here you can divide the minus with the (UpperLimit - LowerLimit)
        angleDist = (a.jointAngles[i] - b.jointAngles[i]) / (1);
        distance += pow(angleDist,2);
    }
    result = sqrt(distance);
    return result;
}

node rrtPlanner::extend(int id, node randNode)
{
    node newNode; 
    node nearestNode = rrtTree[id];
    double distance = calcDist(nearestNode, randNode);
    double step = STEP*JOINTNUM;
    if(distance<=step){
        newNode = randNode;
    }else{
        for(int i=0;i<JOINTNUM;i++){
            // The new node lies between the nearestNode and the randNode
            // with STEP*JOINTNUM distance from nearest
            int newAngle = int(nearestNode.jointAngles[i] + (randNode.jointAngles[i]-nearestNode.jointAngles[i])*step/distance);
            newNode.jointAngles.push_back(newAngle);
        }
    }

    // Check collision of points between nearestNode and newNode 
    bool isFeasible = checkFeasbility(nearestNode, newNode);
    if(isFeasible){
        newNode.prevNodeid = nearestNode.id;
        newNode.id = rrtTree.size();
        return newNode;
    }else{
        newNode.id = -1;
        return newNode;
    }
}

bool rrtPlanner::checkReachGoal(node newNode)
{
    // Use kinematics to calculate the distance between newNode's end-effector's pose and the goalPose
    double distance;
    bool result = true;

    distance = calcDist(newNode, goalNodes[0]);
    if(distance < minGoalDist){minGoalDist = distance;}
    if(distance > maxGoalDist){maxGoalDist = distance;}
    for( size_t j=0;j<JOINTNUM;++j ){
        if( abs(newNode.jointAngles[j] -goalNodes[0].jointAngles[j]) >this->goalToleranceVec[j] ){
            result = false;
            break;
        }
    }
    return result;
}

bool rrtPlanner::checkFeasbility(node a)
{
    // Check angle max and min
    for(int i=0;i<JOINTNUM;++i){
        if(a.jointAngles[i]>jointUpperLimits[i] || a.jointAngles[i]<jointLowerLimits[i]){
            cout << "The angle of " << i << "th joint is out of limit." << endl;
            return false;
        }
    }

    // Check collision
    collision_detection::CollisionRequest collision_request;
    collision_detection::CollisionResult collision_result;
    collision_result.clear();

    const std::vector<std::string>& joint_names = jointModelGroup->getVariableNames();
    std::vector<double> joint_values;
    degreeToRadian(a.jointAngles, &joint_values);

    const std::string PLANNING_SCENE_SERVICE = "get_planning_scene";   
    planningSceneMonitor_->requestPlanningSceneState(PLANNING_SCENE_SERVICE);
    planning_scene_monitor::LockedPlanningSceneRW planningSceneRW(planningSceneMonitor_);
    planningSceneRW->getCurrentStateNonConst().update();

    bool isCollision = false;
    robot_state::RobotState stateInBetween(kinematicModel);
    stateInBetween.setVariablePositions(joint_names, joint_values);
    planningSceneRW->checkCollision(collision_request, collision_result, stateInBetween);
    if(collision_result.collision){
        isCollision = true;
        cout << "The node encounters a collision, node abandoned" << endl;
    }else{
        cout << "This node is not in collision area" << endl;
    }
    return !isCollision;    
}

bool rrtPlanner::checkFeasbility(node nearestNode, node newNode)
{
    // Check angle max and min
    for(int i=0;i<JOINTNUM;++i){
        if(newNode.jointAngles[i]>jointUpperLimits[i] || newNode.jointAngles[i]<jointLowerLimits[i]){
            cout << "The angle of " << i << "th joint is out of limit." << endl;
            return false;
        }
    }

    // Check collision
    collision_detection::CollisionRequest collision_request;
    collision_detection::CollisionResult collision_result;
    collision_result.clear();

    const std::vector<std::string>& joint_names = jointModelGroup->getVariableNames();
    std::vector<double> joint_values;
    degreeToRadian(nearestNode.jointAngles, &joint_values);

    const std::string PLANNING_SCENE_SERVICE = "get_planning_scene";   
    planningSceneMonitor_->requestPlanningSceneState(PLANNING_SCENE_SERVICE);
    planning_scene_monitor::LockedPlanningSceneRW planningSceneRW(planningSceneMonitor_);
    planningSceneRW->getCurrentStateNonConst().update();

    robot_state::RobotState stateInBetween(kinematicModel);
    bool isCollision = false;
    for(size_t i=0;i<FEASI_PIESCES_NUM;i++){
        for(size_t j=0;j<JOINTNUM;j++){
            joint_values[j] += (newNode.jointAngles[j] - nearestNode.jointAngles[j]) * DEG2PI / FEASI_PIESCES_NUM;
            // cout << i << "th between nodes'  " << j << "th joint's angle: " << joint_values[j] << " is checking collision" << endl; 
        }
        stateInBetween.setVariablePositions(joint_names, joint_values);
        planningSceneRW->checkCollision(collision_request, collision_result, stateInBetween);
        if(collision_result.collision){
            isCollision = true;
            cout << i << "th test encounters a collision, node abandoned" << endl;
            break;
        }
    }
    return !isCollision;
}

void rrtPlanner::findPath()
{
    cout << "Starting from the last node: " << rrtTree.size()-1 << endl;
    node curNode = rrtTree.back();
    do{
        this->path.push_back(curNode);
        cout << "Connecting " << curNode.id << "th node with " << curNode.prevNodeid << "th node. " << endl;
        curNode = rrtTree[curNode.prevNodeid];
    }while(curNode.id!=0);
    path.push_back(initialNode);
}

