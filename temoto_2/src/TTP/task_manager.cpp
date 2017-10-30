/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *     MASSIVE TODO: * CATCH ALL EXEPTIONS AND RETHROW AS
 *                     TEMOTO ERROR !!!
 *                   * Start using ros cpp naming conventions
 *                   * Reformat the commented debug messages into
 *                     temoto debug log messages
 *
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "common/tools.h"

#include "TTP/task_manager.h"
#include "TTP/task_descriptor_processor.h"
#include "TTP/task_container.h"
#include "TTP/TTP_errors.h"

#include <algorithm>

namespace TTP
{

struct CandidateInterface
{
    CandidateInterface(TaskInterface inf, unsigned int score):inf_(inf), score_(score){}
    TaskInterface inf_;
    int score_;
};

struct CandidateTask
{
    CandidateTask(TaskDescriptor td, CandidateInterface ci):td_(td), ci_(ci){}
    TaskDescriptor td_;
    CandidateInterface ci_;
};


/* * * * * * * * *
 *  CONSTRUCTOR
 * * * * * * * * */

TaskManager::TaskManager (std::string system_prefix)
    : system_prefix_(system_prefix)
{
    // Initialize the vector of indexed tasks
    //tasks_indexed_ = new std::vector <TaskDescriptor>(1);

    // Construct the classloader
    class_loader_ = new class_loader::MultiLibraryClassLoader(false);

    // Start the servers
    stop_task_server_ = n_.advertiseService (system_prefix_ + "/stop_task", &TaskManager::stopTaskCallback, this);
    //index_tasks_server_ = n_.advertiseService (system_prefix_ + "/index_tasks", &TaskManager::indexTasksCallback, this);

    // Start subscribers
    //stop_task_subscriber_ = n_.subscribe("temoto/stop_task", 10, &TaskManager::stopTaskMsgCallback, this);
}


/* * * * * * * * *
 *  FIND TASK
 * * * * * * * * */

std::vector <TaskDescriptor> TaskManager::findTask(std::string task_to_find, const std::vector <TaskDescriptor>& tasks)
{
    std::vector <TaskDescriptor> tasks_found;
    for (auto& task : tasks)
    {
        if (task_to_find == task.getAction())
        {
            tasks_found.push_back(task);
        }
    }
    return tasks_found;
}


/* * * * * * * * *
 *  FIND TASK LOCAL
 * * * * * * * * */

std::vector <TaskDescriptor> TaskManager::findTaskLocal(std::string task_to_find)
{
    return findTask(task_to_find, tasks_indexed_);
}

/* * * * * * * * *
 *  FIND TASK FROM FILESYSTEM
 * * * * * * * * */

std::vector <TaskDescriptor> TaskManager::findTaskFilesys(std::string task_to_find,
                                                          boost::filesystem::directory_entry base_path,
                                                          int search_depth)
{
    // Name of the method, used for making debugging a bit simpler
    std::string prefix = common::generateLogPrefix("", this->class_name_, __func__);

    boost::filesystem::path current_dir (base_path);
    boost::filesystem::directory_iterator end_itr;
    std::vector <TaskDescriptor> tasks_found;

    try
    {
        // Start looking the files inside current directory
        for ( boost::filesystem::directory_iterator itr( current_dir ); itr != end_itr; ++itr )
        {

            // if its a directory and depth limit is not there yet, go inside it
            if ( boost::filesystem::is_directory(*itr) && (search_depth > 0) )
            {
                std::vector<TaskDescriptor> sub_tasks_found = findTaskFilesys( task_to_find, *itr, (search_depth - 1) );

                // Append the subtasks if not empty
                if ( !sub_tasks_found.empty() )
                {
                    tasks_found.insert(std::end(tasks_found), std::begin(sub_tasks_found), std::end(sub_tasks_found));
                }
            }

            // if its a file and matches the desc file name, process the file
            else if ( boost::filesystem::is_regular_file(*itr) &&
                      ((*itr).path().filename() == description_file_) )
            {
                try
                {
                    /*
                     * Create a description processor object and Get TaskDescriptor
                     * I THINK THIS SHOULD NOT BE CREATED EVERY SINGLE TIME
                     */
                    boost::filesystem::path hackdir ((*itr)); //HACKATON
                    TaskDescriptorProcessor tdp(hackdir.parent_path().string());
                    tasks_found.push_back(tdp.getTaskDescriptor());
                }

                catch( error::ErrorStackUtil & e )
                {
                    // Append the error to local ErrorStack
                    e.forward( prefix );
                    error_handler_.append(e);
                }
            }
        }
        return std::move(tasks_found);
    }
    catch (std::exception& e)
    {
        // Rethrow the exception
        throw error::ErrorStackUtil( TTPErr::FIND_TASK_FAIL,
                                     error::Subsystem::CORE,
                                     error::Urgency::HIGH,
                                     prefix + e.what(),
                                     ros::Time::now() );
    }

    catch(...)
    {
        // Rethrow the exception
        throw error::ErrorStackUtil( TTPErr::UNHANDLED,
                                     error::Subsystem::CORE,
                                     error::Urgency::HIGH,
                                     prefix + "Received an unhandled exception",
                                     ros::Time::now() );
    }
}

/* * * * * * * * *
 *  INDEX TASKS
 * * * * * * * * */

void TaskManager::indexTasks (boost::filesystem::directory_entry base_path, int search_depth)
{
    // Name of the method, used for making debugging a bit simpler
    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

    try
    {
        tasks_indexed_ = findTaskFilesys ("", base_path, search_depth);
    }
    catch( error::ErrorStackUtil & e )
    {
        // Rethrow the exception
        e.forward( prefix );
        throw e;
    }
}


/* * * * * * * * *
 *  GET THE INDEX TASKS
 * * * * * * * * */

std::vector <TaskDescriptor>& TaskManager::getIndexedTasks()
{
    return tasks_indexed_;
}

/* * * * * * * * *
 *  FIND NONSTRICT MATCH
 * * * * * * * * */

unsigned int findNonstrictMatch(const std::vector<Subject>& complete_subjects,
                                const std::vector<Subject>& incomplete_subjects)
{
    // create a copy of complete subjects
    std::vector<Subject> complete_subjects_c = complete_subjects;
    unsigned int score = 0;

    for (auto& i_subject : incomplete_subjects)
    {
        bool subject_match = false;
        for (unsigned int i=0; i<complete_subjects_c.size(); i++)
        {
            // Check for type match
            if (i_subject.type_ != complete_subjects_c[i].type_)
            {
                continue;
            }

            subject_match = true;

            // The more data the complete subject has, the bigger the score
            score += complete_subjects_c[i].data_.size();
            complete_subjects_c.erase(complete_subjects_c.begin() + i);
            break;
        }
        if (subject_match != true)
        {
            return score;
        }
    }
    return score;
}


/* * * * * * * * *
 *  CONNECT TASKS
 * * * * * * * * */

void TaskManager::connectTaskTree(TaskTreeNode& node, std::vector<Subject> parent_subjects, unsigned int depth)
{
    /*
     *
     * TODO: this function is currently quite a mess, a lot of repetitive
     *       stuff going on. Has to be organized
     *
     */

    // Name of the method, used for making debugging a bit simpler
    std::string prefix = common::generateLogPrefix("", class_name_, __func__);

    // If its the root node (depth = 0), then dont look for a task
    if (depth == 0)
    {
        for (auto& child : node.getChildren())
        {
            connectTaskTree(child, parent_subjects, depth + 1);
        }

        return;
    }

    /////////////////////////////    *** PREPARATIONS ***    ////////////////////////////////
    //
    //                  Preparations to manage the upcoming magic

    // A set that contains all tasks that fit the criteria set below. Each matching task has
    // a score that shows how strong the match was.
    std::vector<CandidateTask> candidate_tasks;

    // Variables of the task tree node that are frequently used
    TaskDescriptor& node_task_descriptor = node.getTaskDescriptor();
    const Action& node_action = node_task_descriptor.getAction();
    TaskInterface& node_interface = node_task_descriptor.getInterfaces()[0];
    std::vector<Subject>& node_subjects = node_interface.input_subjects_;

    // A vector that will contain the incomplete i-subjects of all dependent children
    std::vector<Subject> incomplete_subjects;
    //
    //
    /////////////////////////////////////////////////////////////////////////////////////////

    /*
     * Check if this node depends on the parent node. This is done by
     * checking if the task descriptor contains any incomplete subjects
     */
    if (!node_task_descriptor.getIncompleteSubjects().empty())
    {
        //std::cout << "[" << node_action << "] depends on parent node, retrieving missing information ...\n";
        /*
         * Check the node subjects and get the missing information from
         * the parent, making a ALMOST (will get additional information
         * during runtime) complete subject.
         */
        for (auto& n_sub : node_subjects)
        {
            if (n_sub.is_complete_)
            {
                continue;
            }

            // Clean the incomlete subject. This means that all pronouns are removed
            n_sub.words_.clear();

            // go through the parent subjects
            for (auto& p_sub : parent_subjects)
            {
                // If the subject types match, then this is our guy. Copy the contents
                // of the parent subjects into the current incomplete subject
                if (n_sub.type_ == p_sub.type_)
                {
                    /*
                     * TODO: This may create complications at some point, since if parent contains
                     * too much information, e.g., parent has data but the intended child
                     * does not need that, it will get the data regardless. Now if the child
                     * does not have such input interface, its doomed.
                     */
                    n_sub = p_sub;
                }
            }
            //std::cout << "[" << node_action << "] got:\n" << n_sub;
        }
    }

    /*
     * Get the incomplete i-subjects of all dependent children
     */
    for (auto& child : node.getChildren())
    {
        // For every dependent (contains incomplete subjects) child ...
        if (child.getTaskDescriptor().getIncompleteSubjects().size() != 0)
        {
            incomplete_subjects.insert(incomplete_subjects.end(),
                                       child.getTaskDescriptor().getIncompleteSubjects().begin(),
                                       child.getTaskDescriptor().getIncompleteSubjects().end());
        }
    }

    //std::cout << "[" << node_action << "] has " << incomplete_subjects.size() << " incomplete subjects and these are:\n";
    //for (auto& i_s : incomplete_subjects){std::cout << i_s;}

    /*
     * If there were any incomplete subjects then ...
     */
    if (!incomplete_subjects.empty())
    {
        /*
         * Elimineate duplicate i-subjects (based only on type)
         */
        for (unsigned int i=incomplete_subjects.size()-1; i<=0; i--)
        {
            Subject& ics = incomplete_subjects[i];

            /*
             * Start looping backwards, starting from the next element to
             * the left of the ics element
             */
            for (unsigned int j=i-1; j<=0; j--)
            {
                if(incomplete_subjects[j].type_ == ics.type_)
                {
                    // Erase the element
                    incomplete_subjects.erase(incomplete_subjects.begin()+i);
                    break;
                }
            }
        }

        //std::cout << "[" << node_action << "]" << " after eliminating duplicates we have:\n";
        //for (auto& i_s : incomplete_subjects){std::cout << i_s;}

        /*
         * Clean the incomlete subjects. This means that all pronouns are removed
         */
        for (auto& incomplete_subject : incomplete_subjects)
        {
            incomplete_subject.words_.clear();
        }

        //std::cout << "[" << node_action << "]" << " after cleaning the incomplete subs we have:\n";
        //for (auto& i_s : incomplete_subjects){std::cout << i_s;}

        //std::cout << "[" << node_action << "]" << " looking for a suitable task\n";
        /*
         * Find task that matches own i-subjects + that has a matching
         * o-subjects with dependent children incomplete i-subjects
         */
        for (auto& task_descriptor : tasks_indexed_)
        {
            //std::cout << "[" << node_action << "]" << " looking at '" << task_descriptor.getAction() << "'\n";
            //std::cout << "-------------------\n";

            // Look for the task with the same action type
            if (task_descriptor.getAction() != node_action)
            {
                //std::cout << "[" << node_action << "]" << " action does not match\n";
                continue;
            }

            std::vector<CandidateInterface> candidate_interfaces;

            // Loop over the interfaces and ...
            for (auto& interface : task_descriptor.getInterfaces())
            {
                // ... look for STRICT i-subjects match
                if (interface.input_subjects_ != node_subjects)
                {
//                    std::cout << "[" << node_action << "]" << " input subjects do not match\n";
//                    std::cout << "[" << node_action << "]" << " and we are talking about:\n";
//                    std::cout << interface.input_subjects_ << "and\n";
//                    std::cout << node_subjects << "\n";

                    continue;
                }

                // ... look for NONSTRICT o-subjects match
                unsigned int match_score = findNonstrictMatch(interface.output_subjects_, incomplete_subjects);
                if (match_score == 0)
                {
                    //std::cout << "[" << node_action << "]" << " dependent children do not have a match\n";
                    continue;
                }

                // And we have a candidate
                candidate_interfaces.emplace_back(interface, match_score);
            }

            /*
             * If this task contains any suitable interfaces, pick the one with
             * the best match and push the task to the candidate tasks set
             */
            if (!candidate_interfaces.empty())
            {
                //std::cout << "[" << node_action << "]" << " IS A SUITABLE CANDIDATE\n";

                // Sort the candidates with decreasing score and pick the first candidate
                std::sort(candidate_interfaces.begin(),
                          candidate_interfaces.end(),
                          [](CandidateInterface& i1, CandidateInterface& i2)
                          {
                              return i1.score_ > i2.score_;
                          });

                // DEBUG - print out the scores
                //std::cout << "SCORES ";
                //for (auto& inf : candidate_interfaces){std::cout << inf.score_ << ",";} std::cout<<"\n";

                candidate_tasks.emplace_back(task_descriptor, candidate_interfaces[0]);
            }
        }
    }

    /*
     * If there were no incomplete subjects then ...
     */
    else
    {
        /*
         * Find task that matches own i-subjects
         */
        //std::cout << "[" << node_action << "]" << " looking for a suitable task\n";

        for (auto& task_descriptor : tasks_indexed_)
        {
            //std::cout << "[" << node_action << "]" << " looking at '" << task_descriptor.getAction() << "'\n";
            //std::cout << "-------------------\n";

            // Look for the task with the same action type
            if (task_descriptor.getAction() != node_action)
            {
                //std::cout << "[" << node_action << "]" << " action does not match\n";
                continue;
            }

            std::vector<CandidateInterface> candidate_interfaces;

            // Loop over the interfaces and ...
            for (auto& interface : task_descriptor.getInterfaces())
            {
                // ... look for STRICT input subjects match
                if (interface.input_subjects_ != node_subjects)
                {
//                    std::cout << "[" << node_action << "]" << " input subjects do not match\n";
//                    std::cout << "[" << node_action << "]" << " and we are talking about:\n";
//                    std::cout << interface.input_subjects_ << "and\n";
//                    std::cout << node_subjects << "\n";
                    continue;
                }

                // And we have a candidate
                candidate_interfaces.emplace_back(interface, 0);
            }

            /*
             * If this task contains any suitable interfaces, pick the one with
             * the best match and push the task to the candidate tasks set
             */
            if (!candidate_interfaces.empty())
            {
                // Sort the candidates with decreasing score and pick the first candidate
                std::sort(candidate_interfaces.begin(),
                          candidate_interfaces.end(),
                          [](CandidateInterface& i1, CandidateInterface& i2)
                          {
                              return i1.score_ > i2.score_;
                          });

                // DEBUG - print out the scores
                //std::cout << "SCORES ";
                //for (auto& inf : candidate_interfaces){std::cout << inf.score_ << ",";} std::cout<<"\n";

                candidate_tasks.emplace_back(task_descriptor, candidate_interfaces[0]);
            }
        }
    }

    /*
     * If no suitable tasks were found then throw an error
     */
    if (candidate_tasks.empty())
    {
        // Throw error
        error::ErrorStackUtil error_stack_util(TTPErr::NLP_NO_TASK,
                                               error::Subsystem::CORE,
                                               error::Urgency::MEDIUM,
                                               prefix + "Couldn't find a suitable task for action: " + node_action,
                                               ros::Time::now() );
        throw error_stack_util;
    }

    // Sort the candidates with decreasing order and pick the first candidate
    std::sort(candidate_tasks.begin(),
              candidate_tasks.end(),
              [](CandidateTask& t1, CandidateTask& t2)
              {
                  return t1.ci_.score_ > t2.ci_.score_;
              });

    CandidateTask& cand_task = candidate_tasks[0];

    /*
     * Get a bunch of information out of the prime candidate task
     */
    node_interface.id_ = cand_task.ci_.inf_.id_;
    node_interface.type_ = cand_task.ci_.inf_.type_;
    node_interface.output_subjects_ = cand_task.ci_.inf_.output_subjects_;
    node_task_descriptor.task_package_name_ = cand_task.td_.task_package_name_;
    node_task_descriptor.task_lib_path_ = cand_task.td_.task_lib_path_;

    /*
     * Process the child nodes of the tree
     */
    for (auto& child : node.getChildren())
    {
        connectTaskTree(child, node_interface.output_subjects_, depth + 1);
    }
}

/* * * * * * * * *
 *  LOAD AND INITIALIZE TASK TREE
 * * * * * * * * */

void TaskManager::loadAndInitializeTaskTree(TaskTreeNode& node)
{
    // Name of the method, used for making debugging a bit simpler
    std::string prefix = common::generateLogPrefix("", class_name_, __func__);

    // Task descriptor of the node
    TaskDescriptor& node_task_descriptor = node.getTaskDescriptor();

    // If its the root node, then just skip it
    if (node_task_descriptor.getAction() != "ROOT")
    {
        try
        {
            // Load the task
            loadTask(node_task_descriptor);

            // Initialize the task
            instantiateTask(node);
        }
        catch(error::ErrorStackUtil& e)
        {
            e.forward(prefix);
            throw e;
        }
    }

    // Initialize the child tasks
    for (auto& child : node.getChildren())
    {
        loadAndInitializeTaskTree(child);
    }
}


/* * * * * * * * *
 *  LOAD TASK
 * * * * * * * * */

void TaskManager::loadTask(TaskDescriptor& task_descriptor)
{
    // Name of the method, used for making debugging a bit simpler
    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

    /*
     * Create an empty vector for storing the class names (currently a task
     * could have multiple tasks inside it. but it sounds TROUBLESOME
     */
    std::vector<std::string> classes;

    // Get the "path" to its lib file
    std::string path_to_lib =  task_descriptor.getLibPath();

    try
    {
        // Start loading a task library
        ROS_DEBUG("Loading class from path: %s", path_to_lib.c_str());
        class_loader_->loadLibrary(path_to_lib);
        classes = class_loader_->getAvailableClassesForLibrary<BaseTask>(path_to_lib);

        // Done loading
        ROS_DEBUG( "%s Loaded %lu classes from %s", prefix.c_str(), classes.size(), path_to_lib.c_str() );

        // Add the name of the class
        task_descriptor.setTaskClassName(classes[0]);
    }

    // Rethrow the exception
    catch(class_loader::ClassLoaderException& e)
    {
        // Rethrow the exception
        throw error::ErrorStackUtil( TTPErr::CLASS_LOADER_FAIL,
                                     error::Subsystem::CORE,
                                     error::Urgency::HIGH,
                                     prefix + e.what(),
                                     ros::Time::now() );
    }
}


/* * * * * * * * *
 *  INSTANTIATE TASK
 * * * * * * * * */

void TaskManager::instantiateTask(TaskTreeNode& node)
{
    // Name of the method, used for making debugging a bit simpler
    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

    TaskDescriptor& task_descriptor = node.getTaskDescriptor();

    std::string task_class_name = task_descriptor.getTaskClassName();

    // First check that the task has a "class name"
    if (task_class_name.empty())
    {
        throw error::ErrorStackUtil( TTPErr::NAMELESS_TASK_CLASS,
                                     error::Subsystem::CORE,
                                     error::Urgency::HIGH,
                                     prefix + "Task missing a class name",
                                     ros::Time::now() );
    }

    // Check if there is a class with this name
    bool task_class_found = false;
    std::vector<std::string> loaded_classes = class_loader_->getAvailableClasses<BaseTask>();

    for (std::string loaded_class : loaded_classes)
    {
        if (loaded_class == task_class_name)
        {
            task_class_found = true;
            break;
        }
    }

    // If the task was not found, throw an error
    if (!task_class_found)
    {
        throw error::ErrorStackUtil( TTPErr::NO_TASK_CLASS,
                                     error::Subsystem::CORE,
                                     error::Urgency::HIGH,
                                     prefix + "Could not find a task class within loaded classes",
                                     ros::Time::now() );
    }

    // If the task was not found, create an instance of it
    try
    {
        // TODO: DO NOT ACCESS PRIVATE MEMBERS DIRECLY ,ie., UNFRIEND THE TASK MANAGER .. or should I?
        ROS_DEBUG( "%s instatiating task: %s", prefix.c_str(), task_class_name.c_str());
        node.task_pointer_ = class_loader_->createInstance<BaseTask>(task_class_name);
        node.task_pointer_->task_package_name_ = task_descriptor.getTaskPackageName();

        // TODO: This is a hack and it should not exist. But currently this is necessary
        // because flow graph internally updates the task descriptors and the updated
        // task descriptors are needed for stopping the tasks correctly
        node.task_descriptor_ptr_ = boost::make_shared<TaskDescriptor>(task_descriptor);

        // Check if it is a synchronous or asynchronous task
        if (task_descriptor.getFirstInterface().type_ == "asynchronous")
        {
            std::cout << "This is an async task, making a copy of the shared ptr\n";
            asynchronous_tasks_.push_back( {node.task_descriptor_ptr_, node.task_pointer_} );
        }

        return;
    }
    catch(class_loader::ClassLoaderException& e)
    {
        // Rethrow the exception
        throw error::ErrorStackUtil( TTPErr::CLASS_LOADER_FAIL,
                                     error::Subsystem::CORE,
                                     error::Urgency::HIGH,
                                     prefix + e.what(),
                                     ros::Time::now() );
    }
}


/* * * * * * * * *
 *  MAKE FLOW GRAPH
 * * * * * * * * */

void TaskManager::makeFlowGraph(TaskTreeNode& node, tbb::flow::graph& flow_graph)
{
    TaskDescriptor node_task_descriptor = node.getTaskDescriptor();
    boost::shared_ptr<TaskDescriptor> node_task_descriptor_ptr = node.task_descriptor_ptr_;

    // If its the root node, then create the start node
    if (node_task_descriptor.getAction() == "ROOT")
    {
        node.root_fgn_ = std::make_unique<tbb::flow::broadcast_node<Subjects>>(flow_graph);
    }

    // Create a continue node
    else
    {
        node.task_fgn_ = std::make_unique< tbb::flow::function_node<Subjects, Subjects> >
                (flow_graph
                 , tbb::flow::serial
                 , TaskContainer(node.task_pointer_, node_task_descriptor_ptr));
    }

    // Do the same with child nodes
    for (auto& child : node.getChildren())
    {
        makeFlowGraph(child, flow_graph);
    }
}


/* * * * * * * * *
 *  CONNECT FLOW GRAPH
 * * * * * * * * */

void TaskManager::connectFlowGraph(TaskTreeNode& node)
{
    // If its the root node, then create the start edge
    if (node.getTaskDescriptor().getAction() == "ROOT")
    {
        // Connect parent flow graph node with child flow graph nodes
        for (auto& child : node.getChildren())
        {
            tbb::flow::make_edge(*node.root_fgn_, *child.task_fgn_);
            connectFlowGraph(child);
        }
    }
    else
    {
        // Connect parent flow graph node with child flow graph nodes
        for (auto& child : node.getChildren())
        {
            tbb::flow::make_edge(*node.task_fgn_, *child.task_fgn_);
            connectFlowGraph(child);
        }
    }
}


/* * * * * * * * *
 *  STOP TASK SERVICE CALLBACK
 * * * * * * * * */

bool TaskManager::stopTaskCallback( temoto_2::StopTask::Request& req,
                                    temoto_2::StopTask::Response& res)
{
    // Name of the method, used for making debugging a bit simpler
    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

    ROS_INFO( "%s Received a request to stop task. Action = '%s'; what = '%s'"
               , prefix.c_str(), req.action.c_str(), req.what.c_str());

    try
    {
        stopTask(req.action, req.what);

        res.code = 0;
        res.message = "task stopped";
    }

    catch( error::ErrorStackUtil & e )
    {
        // Append the error to local ErrorStack
        e.forward( prefix );
        error_handler_.append(e);

        res.code = 1;
        res.message = "failed to stop the task";
    }

    return true;
}


/* * * * * * * * *
 *  STOP TASK
 * * * * * * * * */

void TaskManager::stopTask(std::string action, std::string what)
{
    // Name of the method, used for making debugging a bit simpler
    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

    bool task_stopped = false;

    std::cout << prefix << " No of asynchronous tasks: " << asynchronous_tasks_.size() << std::endl;

    /*
     * Stop by action and what
     */
    if( !action.empty() && !what.empty() )
    {
        //std::cout << prefix << "Stopping the task based on action and what\n";
        // Look for the task
        for (auto task_it = asynchronous_tasks_.begin();
             task_it != asynchronous_tasks_.end();
             ++task_it)
        {
            //std::cout << prefix << "loop start\n";
            //std::cout << "looking at: " << task_it->first->getAction() << std::endl;
            // Look for the action
            if (task_it->first->getAction() != action)
            {
                continue;
            }
            //std::cout << prefix << "got action\n";
            // Look for the what
            Subjects subjects = task_it->first->getFirstInputSubjects(); // copy
            Subject subject = getSubjectByType("what", subjects);
            //std::cout << prefix << "got what with word size of:" << subject.words_.size() << std::endl;
            if (subject.words_.empty() || subject.words_[0] != what)
            {
                continue;
            }

            //std::cout << prefix << "Fond the task, stopping it\n";
            // Remove the task
            asynchronous_tasks_.erase(task_it);
            task_stopped = true;
            break;
        }
    }

    /*
     * Stop by action
     */
    else if( !action.empty() )
    {
        std::cout << prefix << "Stopping the task based on action\n";
        // Look for the task
        for (auto task_it = asynchronous_tasks_.begin();
             task_it != asynchronous_tasks_.end();
             ++task_it)
        {
            // Look for the action
            if (task_it->first->getAction() != action)
            {
                continue;
            }

            std::cout << prefix << "Fond the task, stopping it\n";
            // Remove the task
            asynchronous_tasks_.erase(task_it);
            task_stopped = true;
            break;
        }
    }

    /*
     * Stop by what
     * In this case everything that corresponds to what is closed
     * ex: "start THE CAMERA and show IT (the camera) in rviz" - both tasks are closed
     */
    else if( !what.empty() )
    {
        std::cout << prefix << "Stopping the task based on what\n";
        // Look for the task
        for (auto task_it = asynchronous_tasks_.begin();
             task_it != asynchronous_tasks_.end(); /* empty */)
        {
            // Look for the what
            Subjects subjects = task_it->first->getFirstInputSubjects(); // copy
            Subject subject = getSubjectByType("what", subjects);
            if (subject.words_.empty() || subject.words_[0] != what)
            {
                task_it++;
                continue;
            }

            std::cout << prefix << "Fond the task, stopping it\n";
            // Remove the task
            asynchronous_tasks_.erase(task_it);
            task_stopped = true;
        }
    }

    if (!task_stopped)
    {
        // If nothing was specified, then throw error
        throw error::ErrorStackUtil( TTPErr::UNSPECIFIED_TASK,
                                     error::Subsystem::CORE,
                                     error::Urgency::MEDIUM,
                                     prefix + "Task 'action' and 'what' unspecified.",
                                     ros::Time::now() );
    }
}
















// OLD STUFF







/* * * * * * * * *
 *  INDEX TASKS SERVICE CALLBACK
 * * * * * * * * */

//bool TaskManager::indexTasksCallback( temoto_2::indexTasks::Request& req,
//                                      temoto_2::indexTasks::Response& res)
//{
//    // Name of the method, used for making debugging a bit simpler
//    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

//    ROS_DEBUG( "%s Received a request to index tasks at '%s'", prefix.c_str(), req.directory.c_str());
//    try
//    {
//        boost::filesystem::directory_entry dir(req.directory);
//        indexTasks(dir, 1);
//        res.code = 0;
//        res.message = "Browsed and indexed the tasks successfully";

//        return true;
//    }
//    catch( error::ErrorStackUtil & e )
//    {
//        // Append the error to local ErrorStack
//        e.forward( prefix );
//        error_handler_.append(e);

//        res.code = 1;
//        res.message = "Failed to index the tasks";
//        return true;
//    }
//}





///* * * * * * * * *
// *  UNLOAD LIB
// * * * * * * * * */

//void TaskManager::unloadTaskLib(std::string path_to_lib)
//{
//    // Name of the method, used for making debugging a bit simpler
//    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

//    try
//    {
//        ROS_DEBUG( "[TaskManager/unloadTaskLib] unloading library");
//        class_loader_->unloadLibrary(path_to_lib);
//    }
//    catch(class_loader::ClassLoaderException& e)
//    {
//        // Rethrow the exception
//        throw error::ErrorStackUtil( TTPErr::CLASS_LOADER_FAIL,
//                                     error::Subsystem::CORE,
//                                     error::Urgency::HIGH,
//                                     prefix + e.what(),
//                                     ros::Time::now() );
//    }
//}




///* * * * * * * * *
// *  START TASK
// *
// *  TODO: * check the running tasks lists before doing anything
// * * * * * * * * */

//void TaskManager::startTask(RunningTask& task, std::vector<boost::any> arguments)
//{
//    // Name of the method, used for making debugging a bit simpler
//    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

//    try
//    {
//        ROS_DEBUG( "%s starting task: %s", prefix.c_str(), task.task_info_.getName().c_str());
//        task.task_pointer_->setID( id_manager_.generateID() );
//        task.task_pointer_->startTaskAutostop( 0, arguments );
//    }

//    catch(...)
//    {
//        // Rethrow the exception
//        throw error::ErrorStackUtil( TTPErr::UNHANDLED,
//                                     error::Subsystem::CORE,
//                                     error::Urgency::HIGH,
//                                     prefix + "Received an unhandled exception",
//                                     ros::Time::now() );
//    }
//}

///* * * * * * * * *
// *  EXECUTE TASK
// * * * * * * * * */

//bool TaskManager::executeTask(TaskDescriptor task_info, std::vector<boost::any> arguments)
//{
//    // Name of the method, used for making debugging a bit simpler
//    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

//    bool start_attempted = false;

//    try
//    {
//        // Create a RunningTask object
//        RunningTask task_to_run;
//        task_to_run.task_info_ = task_info;

//        /*
//         * Use the name of the task to load in the task class. task handler returns the internal
//         * specific name of the task which is used in the next step.
//         */
//        ROS_DEBUG("%s Executing task '%s' ...", prefix.c_str(), task_to_run.task_info_.getName().c_str());

//        // Load the task library
//        loadTask(task_to_run);

//        /*
//         * Create an instance of the task based on the class name. a task .so file might
//         * contain multiple classes, therefore path is not enough and specific name
//         * must be used
//         */
//        instantiateTask(task_to_run);

//        /*
//         * MOVE the task info into the list of running tasks. If the move operation is done
//         * after starting the task, the process will likely crash.
//         */
//        ROS_DEBUG("%s Moving the task into the 'running_tasks' vector ...", prefix.c_str());
//        running_tasks_.push_back (std::move(task_to_run));

//        start_attempted = true;

//        //Start the task
//        startTaskThread(running_tasks_.back(), arguments);
//    }
//    catch( error::ErrorStackUtil & e )
//    {
//        // Append the error to local ErrorStack
//        e.forward( prefix );
//        error_handler_.append(e);

//        if( start_attempted )
//        {
//            try
//            {
//                stopTask(running_tasks_.back().task_info_.getName());
//            }
//            catch( error::ErrorStackUtil & e2 )
//            {
//                e2.forward( prefix );
//                error_handler_.append(e2);
//            }
//        }
//    }
//}


///* * * * * * * * *
// *  stopTaskByID
// * * * * * * * * */

//void TaskManager::stopTaskByID( TemotoID::ID task_id )
//{
//    // Name of the method, used for making debugging a bit simpler
//    std::string prefix = common::generateLogPrefix( system_prefix_, this->class_name_, __func__);

//    try
//    {
//        // Find the task by iterating through the list of running tasks
//        for (auto it = this->running_tasks_.begin(); it != this->running_tasks_.end(); it++)
//        {
//            if ((*it).task_pointer_->getID() == task_id)
//            {
//                ROS_DEBUG( "%s Stopping and erasing task: %s", prefix.c_str(), (*it).task_info_.getName().c_str());
//                /*
//                 * First, call the "stopTask", given that a task is checking the internal "stop_task_" flag
//                 * TODO: Implement a way for brutally forcing  the task thread to stop in case the task is
//                 * stuck inside a loop of some sort
//                 */
//                (*it).task_pointer_->stopTask();

//                // If the task is stopped, join the thread
//                (*it).task_thread_.join();

//                // Delete the task entry, which also calls the destructor of the task
//                running_tasks_.erase (it);

//                ROS_DEBUG( "%s Task stopped", prefix.c_str());

//                return;
//            }
//        }

//        ROS_DEBUG("%s Could not find any tasks with TID: %d", prefix.c_str(), task_id);
//    }
//    catch(class_loader::ClassLoaderException& e)
//    {
//        // Rethrow the exception
//        throw error::ErrorStackUtil( TTPErr::CLASS_LOADER_FAIL,
//                                     error::Subsystem::CORE,
//                                     error::Urgency::HIGH,
//                                     prefix + e.what(),
//                                     ros::Time::now() );
//    }
//    catch(...)
//    {
//        // Rethrow the exception
//        throw error::ErrorStackUtil( TTPErr::UNHANDLED,
//                                     error::Subsystem::CORE,
//                                     error::Urgency::HIGH,
//                                     prefix + "Received an unhandled exception",
//                                     ros::Time::now() );
//    }
//}


///* * * * * * * * *
// *  stopTaskByName
// *  TODO: when multiple tasks with the same name are found then do something more reasonable than
// *        just stopping the first one.
// * * * * * * * * */

//void TaskManager::stopTaskByName( std::string task_name )
//{
//    // Name of the method, used for making debugging a bit simpler
//    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

//    try
//    {
//        // Find the task by iterating through the list of running tasks
//        for (auto it = this->running_tasks_.begin(); it != this->running_tasks_.end(); it++)
//        {
//            // If a task was found then delete it
//            if ((*it).task_info_.getName() == task_name)
//            {
//                ROS_DEBUG( "%s Stopping and erasing task: %s", prefix.c_str(), (*it).task_info_.getName().c_str());
//                /*
//                 * First, call the "stopTask", given that a task is checking the internal "stop_task_" flag
//                 * TODO: Implement a way for brutally forcing  the task thread to stop in case the task is
//                 * stuck inside a loop of some sort
//                 */
//                (*it).task_pointer_->stopTask();

//                // If the task is stopped, join the thread
//                (*it).task_thread_.join();

//                // Delete the task entry, which also calls the destructor of the task
//                running_tasks_.erase (it);

//                ROS_DEBUG( "%s Task stopped", prefix.c_str());

//                return;
//            }
//        }

//        ROS_DEBUG("%s Could not find any tasks named: %s", prefix.c_str(), task_name.c_str());
//    }
//    catch(class_loader::ClassLoaderException& e)
//    {
//        // Rethrow the exception
//        throw error::ErrorStackUtil( TTPErr::CLASS_LOADER_FAIL,
//                                     error::Subsystem::CORE,
//                                     error::Urgency::HIGH,
//                                     prefix + e.what(),
//                                     ros::Time::now() );
//    }
//    catch(...)
//    {
//        // Rethrow the exception
//        throw error::ErrorStackUtil( TTPErr::UNHANDLED,
//                                     error::Subsystem::CORE,
//                                     error::Urgency::HIGH,
//                                     prefix + "Received an unhandled exception.",
//                                     ros::Time::now() );
//    }
//}






///* * * * * * * * *
// *  stopTaskMsgCallback
// * * * * * * * * */

//void TaskManager::stopTaskMsgCallback( temoto_2::StopTaskMsg msg )
//{
//    // Name of the method, used for making debugging a bit simpler
//    std::string prefix = common::generateLogPrefix(system_prefix_, this->class_name_, __func__);

//    ROS_DEBUG( "%s Received a request to stop a task (TID = %ld)", prefix.c_str(), msg.task_id);
//    try
//    {
//         stopTask( "", msg.task_id );
//    }
//    catch( error::ErrorStackUtil & e )
//    {
//        // Append the error to local ErrorStack
//        e.forward( prefix );
//        error_handler_.append(e);
//    }
//}

}// END of TTP namespace