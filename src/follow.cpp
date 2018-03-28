#include <iostream>
#include <ros/ros.h>
#include <geometry_msgs/PoseStamped.h>
#include <nav_msgs/Odometry.h>
#include "std_msgs/Float64.h"
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <mavros_msgs/PositionTarget.h>
#include <unistd.h>
#include <geometry_msgs/Pose2D.h>
#include <mavros_msgs/CommandTOL.h>
#include <time.h>
#include <cmath>
#include <math.h>
#include <ros/duration.h>

using namespace std;

//Set global variables
mavros_msgs::State current_state;
nav_msgs::Odometry current_pose;
geometry_msgs::PoseStamped waypoint;
std_msgs::Float64 current_heading;
float GYM_OFFSET;

//get armed state
void state_cb(const mavros_msgs::State::ConstPtr& msg)
{
  current_state = *msg;
}
//get current position of drone
void pose_cb(const nav_msgs::Odometry::ConstPtr& msg) 
{
	current_pose = *msg;
	ROS_INFO("x: %f y: %f x: %f", current_pose.pose.pose.position.x, current_pose.pose.pose.position.y, current_pose.pose.pose.position.z);
}
//get compass heading 
void heading_cb(const std_msgs::Float64::ConstPtr& msg)
{
  current_heading = *msg;
  //ROS_INFO("current heading: %f", current_heading.data);
}
//set orientation of the drone (drone should always be level)
void setHeading(float heading)
{
  heading = -heading + 90 - GYM_OFFSET;
  float yaw = heading*(M_PI/180);
  float pitch = 0;
  float roll = 0;

  float cy = cos(yaw * 0.5);
  float sy = sin(yaw * 0.5);
  float cr = cos(roll * 0.5);
  float sr = sin(roll * 0.5);
  float cp = cos(pitch * 0.5);
  float sp = sin(pitch * 0.5);

  float qw = cy * cr * cp + sy * sr * sp;
  float qx = cy * sr * cp - sy * cr * sp;
  float qy = cy * cr * sp + sy * sr * cp;
  float qz = sy * cr * cp - cy * sr * sp;

  waypoint.pose.orientation.w = qw;
  waypoint.pose.orientation.x = qx;
  waypoint.pose.orientation.y = qy;
  waypoint.pose.orientation.z = qz;

}
// set position to fly to in the gym frame
void setDestination(float x, float y, float z)
{
  float deg2rad = (M_PI/180);
  float X = x*cos(-GYM_OFFSET*deg2rad) - y*sin(-GYM_OFFSET*deg2rad);
  float Y = x*sin(-GYM_OFFSET*deg2rad) + y*cos(-GYM_OFFSET*deg2rad);
  float Z = z;
  waypoint.pose.position.x = X;
  waypoint.pose.position.y = Y;
  waypoint.pose.position.z = Z;
  ROS_INFO("Destination set to x: %f y: %f z %f", X, Y, Z);
}

//initialize ROS
void init_ros()
{
  ROS_INFO("INITIALIZING ROS");
  for(int i=0; i<100; i++)
  {
    ros::spinOnce();
    ros::Duration(0.01).sleep();
  }
}

// update waypoint from strat but check inputs
void waypoint_update(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
  geometry_msgs::PoseStamped waypoint_eval;
  waypoint_eval = *msg;
  float way_x = waypoint_eval.pose.position.x;
  float way_y = waypoint_eval.pose.position.y;
  float way_z = waypoint_eval.pose.position.z;

  if (way_z < 3 && way_x < 3.6 && way_x > -.6 && way_y < 3.6 && way_y > -.6)
  {
        setDestination(way_x,way_y,way_z);
  }
  else
  {
    ROS_INFO("The waypoint passed is out of bounds at x,y,z = %f,%f,%f",way_x,way_y,way_z);
  }
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "offb_node");
  ros::NodeHandle controlnode;

  // the setpoint publishing rate MUST be faster than 2Hz
  ros::Rate rate(100.0);
  ros::Subscriber state_sub = controlnode.subscribe<mavros_msgs::State>("mavros/state", 10, state_cb);
  ros::Publisher set_vel_pub = controlnode.advertise<mavros_msgs::PositionTarget>("mavros/setpoint_raw/local", 10);
  ros::Publisher local_pos_pub = controlnode.advertise<geometry_msgs::PoseStamped>("mavros/setpoint_position/local", 10);
  ros::Subscriber currentPos = controlnode.subscribe<nav_msgs::Odometry>("mavros/global_position/local", 10, pose_cb);
  ros::Subscriber currentHeading = controlnode.subscribe<std_msgs::Float64>("mavros/global_position/compass_hdg", 10, heading_cb);
  ros::Subscriber waypointSubscrib = controlnode.subscribe<geometry_msgs::PoseStamped>("roombaPose", 10, waypoint_update);

  ros::ServiceClient arming_client = controlnode.serviceClient<mavros_msgs::CommandBool>("mavros/cmd/arming");
  ros::ServiceClient takeoff_client = controlnode.serviceClient<mavros_msgs::CommandTOL>("/mavros/cmd/takeoff");

  //set the orientation of the gym
  GYM_OFFSET = current_heading.data;
  ROS_INFO("the N' axis is facing: %f", GYM_OFFSET);
  cout << current_heading << "\n" << endl;

  // wait for FCU connection
  while (ros::ok() && !current_state.connected)
  {
    ros::spinOnce();
    rate.sleep();
  }
  ROS_INFO("Connected to FCU");

  // wait for mode to be set to guided
  while (current_state.mode != "GUIDED")
  {
    ros::spinOnce();
    rate.sleep();
  }
  ROS_INFO("Mode set to GUIDED");

  // arming
  mavros_msgs::CommandBool arm_request;
  arm_request.request.value = true;
  while (!current_state.armed && !arm_request.response.success)
  {
    ros::Duration(.1).sleep();
    arming_client.call(arm_request);
  }
  ROS_INFO("ARM sent %d", arm_request.response.success);


  //request takeoff
  mavros_msgs::CommandTOL takeoff_request;
  takeoff_request.request.altitude = 3;

  while (!takeoff_request.response.success)
  {
    ros::Duration(.1).sleep();
    takeoff_client.call(takeoff_request);
  }
  ROS_INFO("Takeoff initialized");
  sleep(10);
  setDestination(0,0,3);
  //move foreward
  setHeading(0);
  while(local_pos_pub)
  {
      local_pos_pub.publish(waypoint);
      ros::spinOnce();
      rate.sleep();
  }
  return 0;
}
