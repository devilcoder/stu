#ifndef EXECUTION_HH
#define EXECUTION_HH

/* Code for executing the building process itself.  This is by far the
 * longest source code file in Stu.  Each target is
 * represented at run time by one Execution object.  All Execution
 * objects are allocated with new Execution(...), and are never deleted,
 * as the information contained in them needs to be cached.  All
 * Execution objects are also stored in the map called
 * "executions_by_target" by all their target.  All currently active
 * Execution objects form a rooted acyclic graph.  Note that it is not a
 * tree in the general case; executions may have multiple parents.  But
 * all nodes are reachable from the root node.   
 */

#include <sys/stat.h>

#include "buffer.hh"
#include "build.hh"
#include "job.hh"
#include "link.hh"
#include "parse.hh"
#include "rule.hh"
#include "timestamp.hh"

class Execution
{
public:  

	/* Number of free slots for jobs.  This is a long because
	 * strtol() gives a long.  Parsed from the -j option. 
	 */ 
	static long jobs;

	/* Set once before calling Execution::main().  Unchanging during
	 * the whole call to Execution::main(). 
	 */ 
	static Rule_Set rule_set; 

	/* Main execution loop.  This throws ERROR_BUILD and ERROR_LOGICAL. 
	 */
	static void main(const vector <shared_ptr <Dependency> > &dependencies);

private:

	friend void job_terminate_all(); 
	friend void job_print_jobs(); 

	/* Targets to build.  Empty only for the root target.
	 * Otherwise, all entries have the same dynamic depth.  If the
	 * dynamic depth is larger than one, then there is exactly one
	 * target. 
	 */ 
	vector <Target> targets; 

	/* The instantiated file rule for this execution.  Null when there
	 * is no rule for this file (this happens for instance when a
	 * source code file is given as a dependency, or when this is a
	 * complex dependency).  Individual dynamic dependencies do have
	 * rules, in order for cycles to be detected. 
	 */ 
	shared_ptr <Rule> rule;

	/* The rule from which this execution was derived.  This is
	 * only used to detect strong cycles.  To manage the dependencies, the
	 * instantiated general rule is used.  NULLPTR if and only if RULE is
	 * NULLPTR. 
	 */ 
	shared_ptr <Rule> param_rule;

	/* Currently running executions.  Allocated with new.  Contains
	 * both dependency-subs and dynamic-subs.  
	 */
	unordered_set <Execution *> children;

	/* The parent executions.
	 * This is a map because typically, the number of elements is
	 * always very small, i.e., mostly one, and map is better suited
	 * in this case. 
	 */ 
	map <Execution *, Link> parents; 

	/* The job used to build this file */ 
	Job job;
	
	/* Dependencies that have not yet begun to be built.
	 * Initialized with all dependencies, and emptied over time when
	 * things are built, and filled over time when dynamic dependencies
	 * are worked on. 
	 * Entries are not necessarily unique.  The traces within the
	 * dependencies refer to the declaration of the dependencies,
	 * not to the rules of the dependencies. 
	 */ 
	Buffer buf_default;

	/* The buffer for dependencies in the second pass.  They are only started
	 * if, after (potentially) starting all non-trivial
	 * dependencies, the target must be rebuilt anyway. 
	 */
	Buffer buf_trivial; 

	/* Timestamp of each file targets, before the command is
	 * executed.  Only valid once the job was started.  The indexes
	 * correspond to those in TARGETS.  Non-file indexes are
	 * uninitialized. 
	 * Used for checking whether a file was 
	 * rebuild to decide whether to remove it after a command failed or
	 * was interrupted.  This is UNDEFINED when the file did
	 * not exist, or no target is a file. 
	 */ 
	vector <Timestamp> timestamps_old; 

	/* Variable assignments from parameters for when the command is run. */
	map <string, string> mapping_parameter; 

	/* Variable assignments from actual variables */
	map <string, string> mapping_variable; 

	/* Error value of this target.  The value is propagated (using '|')
	 * to the parent.  Values correspond to constants defined in
	 * error.hh; zero denotes the absence of an error. 
	 */ 
	int error;

	/* What parts of this target have been done. Each bit represents
	 * one aspect that was done.  The depth K is equal to the dynamicity
	 * for dynamic targets, and to zero for non-dynamic targets. 
	 */
	Stack done;

	/* Latest timestamp of a (direct or indirect) file dependency that
	 * was not rebuilt.  (Files that were rebuilt are not
	 * considered, since they make the target be rebuilt anyway.)
	 * The function execute() also changes this to consider the file
	 * itself.  This final timestamp is then carried over to the parent
	 * executions.   
	 */
	Timestamp timestamp; 

	/* Whether this target needs to be built.  When a
	 * target is finished, this value is propagated to the parent
	 * executions (except when the F_EXISTENCE flag is set). 
	 */ 
	bool need_build;

	/* Whether we performed the check in execute().  
	 */ 
	bool checked;

	/* Whether the file targets are known to exist.  
	 *     -1 = at least one file target is known not to exist (only
	 *     	    possible when there is at least one file target)
	 *      0 = status unknown
	 *     +1 = all file targets are known to exist 
	 * When there are no file targets, the value may be both 0 or
	 * +1.  
	 */
	signed char exists;
	
	/* File, transient and dynamic targets.  (Everything except the
	 * root target)
	 */ 
	Execution(Target target_,
		  Link &&link,
		  Execution *parent);

	/* Root execution.  DEPENDENCIES don't have to be unique.
	 */
	Execution(const vector <shared_ptr <Dependency> > &dependencies_); 

	/* There is no implementation of this, as it is not used.  This
	 * serves as a compile-time check that Execution objects are not
	 * destroyed. 
	 */ 
	~Execution(); 

	/* Whether the execution is finished working for the PARENT */ 
	bool finished(Stack avoid) const; 

	/* Whether the execution is completely finished */ 
	bool finished() const;

	/* Read dynamic dependencies from a file.  Can only be called for
	 * dynamic targets.  Called for the parent of a dynamic--file link. 
	 */ 
	void read_dynamics(Stack avoid, shared_ptr <Dependency> dependency_this);

	/* Remove all file targets if they exist.  If OUTPUT is true, output a
	 * corresponding error message.  Return whether the file was
	 * removed.  If OUTPUT is false, only do async signal-safe things. 
	 */
	bool remove_if_existing(bool output); 

	/* Start the next jobs. This will also terminate
	 * jobs when they don't need to be run anymore, and thus it can
	 * be called when K = 0 just to terminate jobs that need to be
	 * terminated.  The passed LINK.FLAG is the ORed combination
	 * of all FLAGs up the dependency chain.  
	 * Return value:  whether additional processes must be started.
	 * Can only by TRUE in random mode.  When TRUE, not all possible
	 * subjobs where started. 
	 */
 	bool execute(Execution *parent, Link &&link);

	/* Execute already-active children.  Parameters 
	 * are equivalent to those of execute(). Return value:
	 * -1: continue; 0: return false; 1: return true.
	 */ 
	int execute_children(const Link &link);

	/* Called after the job was waited for.  The PID is only passed
	 * for checking that it is correct. 
	 */
	void waited(int pid, int status); 

	/* Print full trace for the execution.  First the message is
	 * printed, then all traces for it starting at this execution,
	 * up to the root execution. 
	 * TEXT may be "" to not print any additional message. 
	 */ 
	void print_traces(string text= "") const;

	/* Warn when the file has a modification time in the future */ 
	void warn_future_file(struct stat *buf, const char *filename);

	/* Note:  the top-level flags of LINK.DEPENDENCY may be
	 * modified. 
	 * Return value:  same semantics as for execute(). 
	 */
	bool deploy(const Link &link,
		    const Link &link_child);

	/* Initialize the Execution object.  Used for dynamic dependencies.
	 * Called from get_execution() before the object is connected to a
	 * new parent. */ 
	void initialize(Stack avoid);

	/* Print the command and its associated variable assignments,
	 * according to the selected verbosity level.  
	 * FILENAME_OUTPUT and FILENAME_INPUT are "" when not used. 
	 */ 
	void print_command() const; 

	/* Print a line to stdout as a running job, as output of SIGUSR1.
	 * Is currently running. 
	 */ 
	void print_as_job() const;

	/* All errors by Execution call this function.  Set the error
	 * code, and throw an error except with the keep-going option. 
	 */
	void raise(int error_);

	/* Create the file FILENAME with content from COMMAND */
	void write_content(const char *filename, const Command &command); 

	/* Read the content of the file into a string as the
	 * variable value.  THIS is the variable target.  Return TRUE on
	 * success. 
	 */  
	bool read_variable(string &variable_name,
			   string &content,
			   shared_ptr <Dependency> dependency); 

	bool is_dynamic() const;

	/* The dynamic depth, or -1 for the root execution. */ 
	int get_dynamic_depth() const {
		return targets.empty()
			? -1
			: targets.front().type.get_dynamic_depth(); 
	}

	string verbose_target() const {
		return targets.empty() 
			? "ROOT"
			: targets.front().format_out(); 
	}

	/* The Execution objects by their all of their target.  Execution objects
	 * are never deleted.  This serves as a caching mechanism.  The
	 * root Execution has no targets and therefore is not included.
	 * Non-dynamic execution objects are shared by the multiple
	 * targets of a multi-target rule.  Dynamic multi-target rule
	 * result in multiple non-shared execution objects. 
	 */
	static unordered_map <Target, Execution *> executions_by_target;

	/* The currently running executions by process IDs */ 
	static unordered_map <pid_t, Execution *> executions_by_pid;

	/* The timestamps for transient targets.  This container plays the role of
	 * the file system for transient targets, holding their timestamps, and
	 * remembering whether they have been executed.  Note that if a
	 * rule has both file targets and transient targets, and all
	 * file targets are up to date and the transient targets have
	 * all their dependencies up to date, then the command is not
	 * executed, even though it was never execute in the current
	 * invocation of Stu. In that case, the transient targets are
	 * never insert in this map. 
	 */
	static unordered_map <string, Timestamp> transients;

	/* The timepoint of the last time wait() returned.  No file in the
	 * file system should be newer than this. 
	 */ 
	static Timestamp timestamp_last; 

	/* Whether something was done (either jobs were started or
	 * files where created).  This is tracked for the purpose of the
	 * "Nothing to be done" message. */ 
	static bool worked;

	/* Propagate information from the subexecution to the execution, and
	 * then delete the child execution.  The child execution is
	 * however not deleted as it is kept for caching. 
	 */
	static void unlink(Execution *const parent, 
			   Execution *const child,
			   shared_ptr <Dependency> dependency_parent,
			   Stack avoid_parent,
			   shared_ptr <Dependency> dependency_child,
			   Stack avoid_child,
			   Flags flags_child); 

	/* Get an existing execution or create a new one.
	 * Return NULLPTR when a strong cycle was found; return the execution
	 * otherwise.  PLACE is the place of where the dependency was
	 * declared.  LINK is the link from the existing parent to the
	 * new execution. 
	 */ 
	static Execution *get_execution(const Target &target, 
					Link &&link,
					Execution *parent); 

	/* Wait for next job to finish and finish it.  Do not start anything
	 * new.  
	 */ 
	static void wait();

	/* Find a cycle.  Assuming that the edge parent->child, find a
	 * directed cycle that would be created.  Start at PARENT and
	 * perform a depth-first search upwards in the hierarchy to find
	 * CHILD.  LINK is the LINK that would be added between child
	 * and parent, and would create a cycle. 
	 */
	static bool find_cycle(const Execution *const parent,
			       const Execution *const child,
			       const Link &link);

	static bool find_cycle(vector <const Execution *> &path,
				const Execution *const child,
				const Link &link); 

	/* Print the error message of a cycle on rule level.
	 * Given the path [a, b, c, d, ..., x], the found cycle is
	 * [x <- a <- b <- c <- d <- ... <- x], where A <- B denotes
	 * that A is a dependency of B.  For each edge in this cycle,
	 * output one line.  LINK is the link (x <- a), which is not yet
	 * created in the execution objects. 
	 */ 
	static void cycle_print(const vector <const Execution *> &path,
				const Link &link);

	static bool same_rule(const Execution *execution_a,
			      const Execution *execution_b);
};

unordered_map <pid_t, Execution *> Execution::executions_by_pid;
unordered_map <Target, Execution *> Execution::executions_by_target;
unordered_map <string, Timestamp> Execution::transients;
Timestamp Execution::timestamp_last;
Rule_Set Execution::rule_set; 
long Execution::jobs= 1;
bool Execution::worked= false;

void Execution::wait() 
{
	if (option_verbose) {
		fprintf(stderr, "VERBOSE %s wait\n",
			Verbose::padding()); 
	}

	assert(Execution::executions_by_pid.size()); 

	int status;
	pid_t pid= Job::wait(&status); 

	if (option_verbose) {
		fprintf(stderr, "VERBOSE %s wait pid = %d\n", 
			Verbose::padding(),
			(int) pid);
	}

	timestamp_last= Timestamp::now(); 

	if (executions_by_pid.count(pid) == 0) {
		assert(false);
		return; 
	}

	Execution *const execution= executions_by_pid.at(pid); 

	execution->waited(pid, status); 

	++jobs; 
}

bool Execution::execute(Execution *parent, Link &&link)
{
	Verbose verbose;

	assert(jobs >= 0); 

	for (const Target &target:  targets) {
		assert(target.type.get_dynamic_depth() == done.get_k());
	}
	assert(done.get_k() == link.avoid.get_k()); 

	if (targets.size() && targets.front().type == 0) {
		assert(link.avoid.get_lowest() == (link.flags & ((1 << F_COUNT) - 1))); 
	}
	done.check();

	if (option_verbose) {
		string text_target= verbose_target(); 
		string text_flags= flags_format(link.flags);
		string text_avoid= link.avoid.format(); 

		fprintf(stderr, "VERBOSE %s %s execute %s %s\n", 
			Verbose::padding(),
			text_target.c_str(),
			text_flags.c_str(),
			text_avoid.c_str()); 
	}

	/* Override the trivial flag */ 
	if (link.flags & F_OVERRIDETRIVIAL) {
		link.flags &= ~F_TRIVIAL; 
		link.avoid.rem_highest(F_TRIVIAL); 
	}

 	if (finished(link.avoid)) {
		if (option_verbose) {
			string text_target= verbose_target(); 
			fprintf(stderr, "VERBOSE %s %s finished\n",
				Verbose::padding(),
				text_target.c_str());
		}
		return false;
	}

	/* In DFS mode, first continue the already-open children, then
	 * open new children.  In random mode, start new children first
	 * and continue already-open children second */ 

	/* 
	 * Continue the already-active child executions 
	 */  

	if (order != Order::RANDOM) {
		int ret= execute_children(link);
		if (ret >= 0)
			return ret;
	}

	/* Should children even be started?  Check whether this is an
	 * optional dependency and if it is, return when the file does not
	 * exist.  
	 */
	if ((link.flags & F_OPTIONAL) 
	    && link.dependency != nullptr
	    && dynamic_pointer_cast <Direct_Dependency> (link.dependency)
	    && dynamic_pointer_cast <Direct_Dependency> (link.dependency)
	    ->place_param_target.type == Type::FILE) {

		const char *name= dynamic_pointer_cast <Direct_Dependency> (link.dependency)
			->place_param_target.place_param_name.unparametrized().c_str();

		struct stat buf;
		int ret_stat= stat(name, &buf);
		if (ret_stat < 0) {
			exists= -1;
			if (errno != ENOENT) {
				print_error_system(name); 
				raise(ERROR_BUILD);
				done.add_neg(link.avoid); 
				return false;
			}
			done.add_highest_neg(link.avoid.get_highest()); 
			return false;
		} else {
			assert(ret_stat == 0);
			exists= +1;
		}
	}

	/* Is this a trivial dependency and we are not in trivial
	 * override mode?  Then skip the dependency. */
	if (link.flags & F_TRIVIAL) {
		done.add_neg(link.avoid);
		return false;
	}

	if (targets.empty())
		assert(done.get_k() == 0);
	else 
		assert(done.get_k() == targets.front().type.get_dynamic_depth());

	if (error) 
		assert(option_keep_going); 

	/* 
	 * Deploy dependencies (first pass), with the F_NOTRIVIAL flag
	 */ 
	while (! buf_default.empty()) {
		Link link_child= buf_default.next(); 
		Link link_child_overridetrivial= link_child;
		link_child_overridetrivial.avoid.add_highest(F_OVERRIDETRIVIAL); 
		link_child_overridetrivial.flags |= F_OVERRIDETRIVIAL; 
		buf_trivial.push(move(link_child_overridetrivial)); 
		if (deploy(link, link_child))
			return true;
		if (jobs == 0)
			return false;
	} 
	assert(buf_default.empty()); 

	if (order == Order::RANDOM) {
		int ret= execute_children(link);
		if (ret >= 0)
			return ret;
	}

	/* Some dependencies are still running */ 
	if (children.size())
		return false;

	/* There was an error in a child */ 
	if (error != 0) {
		assert(option_keep_going == true); 
		done.add_neg(link.avoid);
		return false;
	}

	/* Rule does not have a command.  This includes the case of dynamic
	 * executions, even though for dynamic executions the RULE variable
	 * is set (to detect cycles). */ 
	/* We cannot return here in the non-dynamic case, because we
	 * must still check that the target files exist, even if they
	 * don't have commands. */ 
	if (targets.empty() || get_dynamic_depth() != 0) {
		done.add_neg(link.avoid);
		return false;
	}

	/* Job has already been started */ 
	if (job.started_or_waited()) {
		return false;
	}

	/* Build the file itself */ 

	assert(jobs > 0); 
	assert(! targets.empty());
	assert(targets.front().type.get_dynamic_depth() == 0); 
	assert(targets.back().type.get_dynamic_depth() == 0); 
	assert(buf_default.empty()); 
	assert(children.empty()); 
	assert(error == 0);

	/*
	 * Check whether execution has to be built
	 */

	/* Check existence of file */
	timestamps_old.assign(targets.size(), Timestamp::UNDEFINED); 

	/* A target for which no execution has to be done */ 
	const bool no_execution= 
		rule != nullptr && rule->command == nullptr && ! rule->is_copy;

	if (! checked) {
		checked= true; 

		/* Set to -1 when a file is found not to exist */ 
		exists= +1; 

		for (unsigned i= 0;  i < targets.size();  ++i) {
			const Target &target= targets[i]; 

			if (target.type != Type::FILE) 
				continue;

			/* We save the return value of stat() and handle errors later */ 
			struct stat buf;
			int ret_stat= stat(target.name.c_str(), &buf);

			/* Warn when file has timestamp in the future */ 
			if (ret_stat == 0) { 
				/* File exists */ 
				Timestamp timestamp_file= Timestamp(&buf); 
				timestamps_old[i]= timestamp_file;
 				if (parent == nullptr || ! (link.flags & F_EXISTENCE)) 
					warn_future_file(&buf, target.name.c_str());
				/* EXISTS is not changed */ 
			} else 
				exists= -1;

			if (! need_build && ret_stat == 0 &&
			    timestamp.defined() && timestamps_old[i] < timestamp &&
			    ! no_execution) {
				need_build= true;
			}

			if (ret_stat == 0) {
				/* File exists. Check whether it has to be rebuilt
				 * because of more up to date dependencies */ 

				assert(timestamps_old[i].defined()); 
				if (timestamp.defined() && 
				    timestamps_old[i] < timestamp) {
					if (no_execution) {
						print_warning
							(fmt("File target %s which has no command is older than its dependency",
							     target.format_err()));
					}
				} 
			}
			
			if (! need_build && ret_stat != 0 && errno == ENOENT) {
				/* File does not exist */

				if (! (link.flags & F_OPTIONAL)) {
					/* Non-optional dependency */  
					need_build= true; 
				} else {
					/* Optional dependency:  don't create the file;
					 * it will then not exist when the parent is
					 * called. */ 
					done.add_one_neg(F_OPTIONAL); 
					return false;
				}
			}

			if (ret_stat != 0 && errno != ENOENT) {
				/* stat() returned an actual error,
				 * e.g. permission denied:  build error */
				print_error_system(target.name.c_str());
				raise(ERROR_BUILD);
				done.add_one_neg(link.avoid); 
				return false;
			}

			/* File does not exist, all its dependencies are up to
			 * date, and the file has no commands: that's an error */  
			if (ret_stat != 0 && no_execution) { 

				assert(errno == ENOENT); 

				if (rule->dependencies.size()) {
					if (output_mode > Output::SILENT)
						print_traces
							(fmt("file without command %s does not exist, although all its dependencies are up to date", 
							     target.format_err())); 
					explain_file_without_command_with_dependencies(); 
				} else {
					if (output_mode > Output::SILENT)
						rule->place_param_targets[i]->place
							<< fmt("file without command and without dependencies %s does not exist",
							       target.format_err()); 
						print_traces();
					explain_file_without_command_without_dependencies(); 
				}
				done.add_one_neg(link.avoid); 
				raise(ERROR_BUILD);
				return false;
			}		
		}
		
		/* We cannot update TIMESTAMP within the loop above
		 * because we need to compare each TIMESTAMP_OLD with
		 * the previous value of TIMESTAMP. 
		 */
		for (Timestamp timestamp_old_i:  timestamps_old) {
			if (timestamp_old_i.defined() &&
			    (! timestamp.defined() || timestamp < timestamp_old_i)) {
				timestamp= timestamp_old_i; 
			}
		}
	}

	if (! need_build) {
		bool has_file= false;
		for (const Target &target:  targets) {
			if (target.type == Type::FILE) {
				has_file= true; 
			}
		}
		for (const Target &target:  targets) {
			if (target.type != Type::TRANSIENT) 
				continue; 
			if (transients.count(target.name) == 0) {
				/* Transient was not yet executed */ 
				if (! no_execution && ! has_file) {
					need_build= true; 
				}
				break;
			}
		}
	}

	if (! need_build) {
		/* The file does not have to be built */ 
		done.add_neg(link.avoid);
		return false;
	}

	/*
	 * The command must be run now, or there is no command. 
	 */

	/*
	 * Re-deploy all dependencies (second pass)
	 */
	while (! buf_trivial.empty()) {
		Link link_child= buf_trivial.next(); 
		if (deploy(link, link_child))
			return true;
		if (jobs == 0)
			return false;
	} 
	assert(buf_trivial.empty()); 

	if (no_execution) {
		/* A target without a command */ 
		done.add_neg(link.avoid); 
		return false;
	}

	/* The file must be created now */

	if (option_question) {
		if (output_mode > Output::SILENT) 
			puts("Targets are not up to date");
		exit(ERROR_BUILD);
	}

	worked= true; 
	
	print_command();

	if (rule->is_hardcode) {
		assert(targets.size() == 1);
		assert(targets.front().type == Type::FILE); 
		
		done.add_one_neg(0); 

		if (option_verbose) {
			string text_target= verbose_target();
			fprintf(stderr, "VERBOSE %s %s create content\n",
				Verbose::padding(),
				text_target.c_str());
		}

		write_content(targets.front().name.c_str(), *(rule->command)); 
		return false;
	}

	/* Start the job */ 

	for (const Target &target:  targets) {
		if (target.type != Type::TRANSIENT)  
			continue; 
		Timestamp timestamp_now= Timestamp::now(); 
		assert(timestamp_now.defined()); 
		assert(transients.count(target.name) == 0); 
		transients[target.name]= timestamp_now; 
	}

	if (rule->redirect_index >= 0)
		assert(rule->place_param_targets[rule->redirect_index]->type == Type::FILE); 

	assert(jobs >= 1); 

	map <string, string> mapping;
	mapping.insert(mapping_parameter.begin(), mapping_parameter.end());
	mapping.insert(mapping_variable.begin(), mapping_variable.end());
	mapping_parameter.clear();
	mapping_variable.clear(); 

	pid_t pid; 
	{
		/* Block signals from the time the process is started,
		 * to after we have entered it in the map */
		Job::Signal_Blocker sb;

		if (rule->is_copy) {

			assert(rule->place_param_targets.size() == 1); 
			assert(rule->place_param_targets.front()->type == Type::FILE); 
			
			pid= job.start_copy
				(rule->place_param_targets[0]->place_param_name.unparametrized(),
				 rule->filename.unparametrized());
		} else {
			pid= job.start
				(rule->command->command, 
				 mapping,
				 rule->redirect_index < 0 ? "" :
				 rule->place_param_targets[rule->redirect_index]
				 ->place_param_name.unparametrized(),
				 rule->filename.unparametrized(),
				 rule->command->place); 
		}


		assert(pid != 0 && pid != 1); 

		if (option_verbose) {
			string text_target= verbose_target();
			fprintf(stderr, "VERBOSE %s %s execute pid = %d\n", 
				Verbose::padding(),
				text_target.c_str(),
				(int)pid); 
		}

		if (pid < 0) {
			/* Starting the job failed */ 
			if (output_mode > Output::SILENT)
				print_traces(fmt("error executing command for %s", 
						 targets.front().format_err())); 
			raise(ERROR_BUILD);
			done.add_neg(link.avoid); 
			return false;
		}

		executions_by_pid[pid]= this;
	}

	assert(executions_by_pid.at(pid)->job.started()); 
	assert(pid == executions_by_pid.at(pid)->job.get_pid()); 
	--jobs;
	assert(jobs >= 0);

	if (order == Order::RANDOM) {
		return jobs > 0; 
	} else if (order == Order::DFS) {
		return false;
	} else {
		assert(false);
		return false;
	}
}

int Execution::execute_children(const Link &link)
{
	/* Since unlink() may change execution->children,
	 * we must first copy it over locally, and then iterate
	 * through it */ 

	vector <Execution *> executions_children_vector
		(children.begin(), children.end()); 

	while (! executions_children_vector.empty()) {

		if (order_vec) {
			/* Exchange a random position with last position */ 
			size_t p_last= executions_children_vector.size() - 1;
			size_t p_random= random_number(executions_children_vector.size());
			if (p_last != p_random) {
				swap(executions_children_vector[p_last],
				     executions_children_vector[p_random]); 
			}
		}

		Execution *child= executions_children_vector.at
			(executions_children_vector.size() - 1);
		executions_children_vector.resize(executions_children_vector.size() - 1); 
		
		assert(child != nullptr);

		Stack avoid_child= child->parents.at(this).avoid;
		Flags flags_child= child->parents.at(this).flags;

		if (link.dependency != nullptr 
		    && dynamic_pointer_cast <Direct_Dependency> (link.dependency)
		    && dynamic_pointer_cast <Direct_Dependency> (link.dependency)
		    ->place_param_target.type == Type::TRANSIENT) {
			flags_child |= link.flags; 
		}

		shared_ptr <Dependency> dependency_child= child->parents.at(this).dependency;
		
		Link link_child(avoid_child, flags_child, child->parents.at(this).place,
				dependency_child);

		if (child->execute(this, move(link_child)))
			return 1;
		assert(jobs >= 0);
		if (jobs == 0)  
			return 0;

		if (child->finished(avoid_child)) {
			unlink(this, child, 
			       link.dependency,
			       link.avoid, 
			       dependency_child, avoid_child, flags_child); 
		}
	}

	if (error) 
		assert(option_keep_going); 

	return -1;
}

void Execution::waited(int pid, int status) 
{
	assert(job.started()); 
	assert(job.get_pid() == pid); 
	assert(buf_default.empty()); 
	assert(buf_trivial.empty()); 
	assert(children.size() == 0); 

	assert(done.get_k() == 0);
	done.add_one_neg(0); 

	{
		Job::Signal_Blocker sb;
		executions_by_pid.erase(pid); 
	}

	/* The file(s) may have been built, so forget that it was known to
	 * not exist */
	if (exists < 0)  
		exists= 0;
	
	if (job.waited(status, pid)) {
		/* Command was successful */ 

		/* Set to -1 if at least one target file is missing */
		exists= +1; 

		/* For file targets, check that the file was built */ 
		for (const Target &target:  targets) {

			if (target.type != Type::FILE)
				continue;

			struct stat buf;

			if (0 == stat(target.name.c_str(), &buf)) {
				/* Check that the file was not created with modification
				 * time in the future */  
				warn_future_file(&buf, target.name.c_str()); 
				/* Check that file is not older that Stu
				 * startup */ 
				Timestamp timestamp_file(&buf);

				if (! timestamp.defined() ||
				    timestamp < timestamp_file)
					timestamp= timestamp_file; 

				/* Check whether the just created file
				 * is older than Stu startup */ 
				if (timestamp_file < Timestamp::startup) {
					/* Check whether the file is actually a symlink, in
					 * which case we ignore that error */ 
					if (0 > lstat(target.name.c_str(), &buf)) {
						print_error_system(target.name.c_str()); 
						raise(ERROR_BUILD);
					}
					if (! S_ISLNK(buf.st_mode)) {
						rule->place 
							<< fmt("timestamp of file %s after execution of its command is older than %s startup", 
							       target.format_err(), 
							       dollar_zero)
							<< fmt("timestamp of %s is %s",
							       target.format_err(), timestamp_file.format())
							<< fmt("startup timestamp is %s", 
							       Timestamp::startup.format()); 
						print_traces();
						explain_startup_time();
						raise(ERROR_BUILD);
					}
				}
			} else {
				exists= -1;
				rule->command->place <<
					fmt("file %s was not built by command", 
					    target.format_err()); 
				print_traces();
				raise(ERROR_BUILD);
			}
		}

	} else {
		/* Command failed */ 
		
		if (output_mode > Output::SILENT) {
			string reason;
			if (WIFEXITED(status)) {
				reason= frmt("failed with exit code %d", WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				int sig= WTERMSIG(status);
				reason= frmt("received signal %d (%s)", 
					     sig,
					     strsignal(sig));
			} else {
				/* This should not happen but the standard does not exclude
				 * it */ 
				reason= frmt("failed with status code %d", status); 
			}

			
			if (! param_rule->is_copy) {
				Target target= parents.begin()->second.dependency->get_single_target().unparametrized(); 
				param_rule->command->place <<
					fmt("command for %s %s", 
					    target.format_err(), 
					    reason); 
			} else {
				/* Copy rule */
				param_rule->place <<
					fmt("cp to %s %s", targets.front().format_err(), reason); 
			}

			print_traces(); 
		}

		remove_if_existing(true); 

		raise(ERROR_BUILD);
	}
}

void Execution::main(const vector <shared_ptr <Dependency> > &dependencies)
{
	assert(jobs >= 0);

	timestamp_last= Timestamp::now(); 

	Execution *execution_root= new Execution(dependencies); 

	int error= 0; 

	try {
		while (! execution_root->finished()) {

			Link link(Stack(), (Flags)0, Place(), shared_ptr <Dependency> ());

			bool r;

			do {
				if (option_verbose) {
					fprintf(stderr, "VERBOSE %s main.next\n", 
						Verbose::padding());
				}
				r= execution_root->execute(nullptr, move(link));
			} while (r);

			if (executions_by_pid.size()) {
				wait();
			}
		}

		assert(execution_root->finished()); 

		bool success= (execution_root->error == 0);

		if (! option_keep_going)
			assert(success); 

		if (success && ! worked && output_mode > Output::SILENT) {
			puts("Nothing to be done"); 
		}

		if (! success && option_keep_going) {
			print_info("Targets not rebuilt because of errors"); 
		}

		error= execution_root->error; 
		assert(error >= 0 && error <= 3); 
	} 

	/* A build error is only thrown when options_continue is
	 * not set */ 
	catch (int e) {

		assert(! option_keep_going); 
		assert(e >= 1 && e <= 4); 

		/* Terminate all jobs */ 
		if (executions_by_pid.size()) {
			print_error("Terminating all running jobs"); 
			job_terminate_all();
		}

		if (e == ERROR_FATAL)
			exit(ERROR_FATAL); 

		error= e; 
	}

	if (error)
		throw error; 
}

void Execution::unlink(Execution *const parent, 
		       Execution *const child,
		       shared_ptr <Dependency> dependency_parent,
		       Stack avoid_parent,
		       shared_ptr <Dependency> dependency_child,
		       Stack avoid_child,
		       Flags flags_child)
{
	(void) avoid_child;

	if (option_verbose) {
		string text_parent= parent->verbose_target();
		string text_child= child->verbose_target();
		string text_done_child= child->done.format();
		fprintf(stderr, "VERBOSE %s %s unlink %s %s\n",
			Verbose::padding(),
			text_parent.c_str(),
			text_child.c_str(),
			text_done_child.c_str());
	}

	assert(parent != child); 
	assert(child->finished(avoid_child)); 

	if (! option_keep_going)  
		assert(child->error == 0); 

	/*
	 * Propagations
	 */

	/* Propagate dynamic dependencies */ 
	if (flags_child & F_READ) {
		
		/* Always in a [...[A]...] -> A link */

		assert(dynamic_pointer_cast <Direct_Dependency> (dependency_child)
		       && dynamic_pointer_cast <Direct_Dependency> (dependency_child)
		       ->place_param_target.type == Type::FILE);

		assert(dependency_parent->get_single_target().type.is_dynamic());
		assert(dependency_parent->get_single_target().type.is_any_file());

		assert(parent->targets.size() == 1);
#ifndef NDEBUG
		bool found= false;
		for (const Target &target:  child->targets) {
			if (target.name == parent->targets.front().name)
				found= true;
		}
		assert(found); 
#endif 

		assert(child->done.get_k() == 0); 
		
		bool do_read= true;

		if (child->error != 0) {
			do_read= false;
		}

		/* Don't read the dependencies when the target was optional and
		 * was not built */
		/* child->exists was set to +1 earlier when the optional
		 * dependency was found to exist
		 */
		else if (flags_child & F_OPTIONAL) {
			if (child->exists <= 0) {
				do_read= false;
			}
		}

		if (do_read) {
			parent->read_dynamics(avoid_parent, dependency_parent); 
		}
	}

	/* Propagate timestamp.  Note:  When the parent execution has
	 * filename == "", this is unneccesary, but it's easier to not
	 * check, since that happens only once. */
	/* Don't propagate the timestamp of the dynamic dependency itself */ 
	if (! (flags_child & F_EXISTENCE) && ! (flags_child & F_READ)) {
		if (child->timestamp.defined()) {
			if (! parent->timestamp.defined()) {
				parent->timestamp= child->timestamp;
			} else {
				if (parent->timestamp < child->timestamp) {
					parent->timestamp= child->timestamp; 
				}
			}
		}
	}

	/* Propagate variable dependencies */
	if ((flags_child & F_VARIABLE) && child->exists > 0) {

		string variable_name;
		string content;
		if (child->read_variable(variable_name, content, dependency_child))
			parent->mapping_variable[variable_name]= content;
	}

	/*
	 * Propagate variables over transient targets without commands and dynamic
	 * targets
	 */
	if (child->is_dynamic() ||
	    (dynamic_pointer_cast <Direct_Dependency> (dependency_child)
	     && dynamic_pointer_cast <Direct_Dependency> (dependency_child)->place_param_target.type == Type::TRANSIENT
	     && child->rule != nullptr 
	     && child->rule->command == nullptr)) {
		parent->mapping_variable.insert
			(child->mapping_variable.begin(), child->mapping_variable.end()); 
	}

	/* 
	 * Propagate attributes
	 */ 

	/* Note:  propagate the flags after propagating other things, since
	 * flags can be changed by the propagations done before. 
	 */

	parent->error |= child->error; 

	if (child->need_build 
	    && ! (flags_child & F_EXISTENCE)
	    && ! (flags_child & F_READ)) {
		parent->need_build= true; 
	}

	/* 
	 * Remove the links between them 
	 */ 

	assert(parent->children.count(child) == 1); 
	parent->children.erase(child);

	assert(child->parents.count(parent) == 1);
	child->parents.erase(parent);
}

Execution::Execution(Target target_,
		     Link &&link,
		     Execution *parent)
	:  error(0),
	   done(target_.type.get_dynamic_depth(), 0),
	   timestamp(Timestamp::UNDEFINED),
	   need_build(false),
	   checked(false),
	   exists(0)
{
	assert(parent != nullptr); 
	assert(parents.empty()); 

	/* Fill in the rules and their parameters */ 
	if (target_.type == Type::FILE || target_.type == Type::TRANSIENT) {
		rule= rule_set.get(target_, param_rule, mapping_parameter); 
		if (rule == nullptr) {
			targets.push_back(target_); 
		} else {
			for (auto &place_param_target:  rule->place_param_targets) {
				targets.push_back(place_param_target->unparametrized()); 
			}
		}
	} else {
		assert(target_.type.is_dynamic()); 
		/* We must set the rule here, so cycles in the dependency graph
		 * can be detected.  Note however that the rule of dynamic
		 * file dependency executions is otherwise not used */ 
		Target target_base(target_.type.get_base(), target_.name);
		rule= rule_set.get(target_base, param_rule, mapping_parameter); 

		/* For dynamic executions, the TARGETS variables
		 * contains only a single target */ 
		targets.push_back(target_); 
	}
	assert((param_rule == nullptr) == (rule == nullptr)); 


	if (option_verbose) {
		string text_target= verbose_target();
		string text_rule= rule == nullptr ? "(no rule)" : rule->format(); 
		fprintf(stderr, "VERBOSE  %s   %s %s\n",
			Verbose::padding(),
			text_target.c_str(),
			text_rule.c_str()); 
	}

	parents[parent]= link; 

	for (const Target &target:  targets) {
		executions_by_target[target]= this; 
	}

	if (! (
	       target_.type.is_dynamic() && target_.type.is_any_file()
	       ) 
	    && rule != nullptr) {

		/* There is a rule for this execution */ 
		for (auto &dependency:  rule->dependencies) {
			assert(! dependency->get_place().empty());

			shared_ptr <Dependency> dep= dependency;
			if (target_.type.is_any_transient()) {
				dep->add_flags(link.avoid.get_lowest());
			
				for (unsigned i= 0;  i < target_.type.get_dynamic_depth();  ++i) {
					Flags flags= link.avoid.get(i + 1);
					dep= make_shared <Dynamic_Dependency> (flags, dep);
				}
			} 

			Link link_new(dep); 

			if (option_verbose) {
				string text_target= verbose_target();
				string text_link_new= link_new.format(); 
				fprintf(stderr, "VERBOSE %s    %s push %s\n",
					Verbose::padding(),
					text_target.c_str(),
					text_link_new.c_str()); 
			}
			buf_default.push(move(link_new)); 
		}
	} else {
		/* There is no rule */ 

		/* Whether to produce the "rule not found" error */ 
		bool rule_not_found= false;

		if (target_.type == Type::FILE) {
			if (! (link.flags & F_OPTIONAL)) {
				/* Check that the file is present,
				 * or make it an error */ 
				struct stat buf;
				int ret_stat= stat(target_.name.c_str(), &buf);
				if (0 > ret_stat) {
					if (errno != ENOENT) {
						print_error_system(target_.name.c_str()); 
						raise(ERROR_BUILD); 
					}
					/* File does not exist and there is no rule for it */ 
					error |= ERROR_BUILD;
					rule_not_found= true;
				} else {
					/* File exists:  Do nothing, and there are no
					 * dependencies to build */  
					if (parent->targets.empty()
					    && output_mode > Output::SILENT) {
						/* Output this only for top-level targets, and
						 * therefore we don't need traces */ 
						/* We don't use color
						 * for the filename because
						 * the output goes to
						 * stdout instead of
						 * stderr */
						printf("No rule for building '%s', but the file exists\n", 
						       target_.name.c_str()); 
					} 
				}
			}
		} else if (target_.type == Type::TRANSIENT) {
			rule_not_found= true;
		} else {
			assert(target_.type.is_dynamic()); 
		}
		
		if (rule_not_found) {
			assert(rule == nullptr); 
			if (output_mode > Output::SILENT) {
				print_traces(fmt("no rule to build %s", 
						 target_.format_err()));
			}
			raise(ERROR_BUILD);
			/* Even when a rule was not found, the Execution object remains
			 * in memory */  
		}
	}

}

Execution::Execution(const vector <shared_ptr <Dependency> > &dependencies_)
	:  error(0),
	   need_build(false),
	   checked(false),
	   exists(0)
{
	for (auto &i:  dependencies_) {
		buf_default.push(Link(i)); 
	}
}

bool Execution::finished(Stack avoid) const
{
	assert(avoid.get_k() == done.get_k());

	if (targets.empty())
		assert(done.get_k() == 0);
	else {
		assert(done.get_k() == targets.front().type.get_dynamic_depth());
		assert(done.get_k() == targets.back().type.get_dynamic_depth());
	}

	Flags to_do_aggregate= 0;
	
	for (unsigned j= 0;  j <= done.get_k();  ++j) {
		to_do_aggregate |= ~done.get(j) & ~avoid.get(j); 
	}

	return (to_do_aggregate & ((1 << F_COUNT) - 1)) == 0; 
}

bool Execution::finished() const 
{
	if (targets.empty())
		assert(done.get_k() == 0);
	else 
		assert(done.get_k() == targets.front().type.get_dynamic_depth());

	Flags to_do_aggregate= 0;
	
	for (unsigned j= 0;  j <= done.get_k();  ++j) {
		to_do_aggregate |= ~done.get(j); 
	}

	return (to_do_aggregate & ((1 << F_COUNT) - 1)) == 0; 
}

/* The declaration of this function is in job.hh */ 
void job_terminate_all() 
{
	/* Strictly speaking, there is a bug here because the C++
	 * containers are not async signal safe */ 

	for (auto i= Execution::executions_by_pid.begin();
	     i != Execution::executions_by_pid.end();  ++i) {

		const pid_t pid= i->first;
		
		assert(pid > 1); 

		/* Passing (-pid) to kill() kills the whole process
		 * group with PGID (pid).  Since we set each child
		 * process to have its PID as its process group ID,
		 * this kills the child and all its children
		 * (recursively), up to programs that change this PGID
		 * of processes, such as Stu and shells, which have to
		 * kill their children explicitly in their signal
		 * handlers.  */ 
		if (0 > kill(-pid, SIGTERM)) {
			if (errno == ESRCH) {
				/* The child process is a zombie.  This
				 * means the child process has already
				 * terminated but we haven't wait()ed
				 * for it yet. */ 
			} else {
				write_safe(2, "*** Error: Kill\n"); 
				/* Note:  Don't call exit() yet; we want all
				 * children to be killed. */ 
			}
		}
	}

	bool terminated= false;

	for (auto i= Execution::executions_by_pid.begin();
	     i != Execution::executions_by_pid.end();  ++i) {

		if (i->second->remove_if_existing(false))
			terminated= true; 
	}

	if (terminated) {
		write_safe(2, "Removing partially built files\n");
	}

	/* Check that all children are terminated */ 
	while (true) {
		int status;
		int ret= wait(&status); 

		if (ret < 0) {
			/* wait() sets errno to ECHILD when there was no
			 * child to wait for */ 
			if (errno != ECHILD) {
				write_safe(2, "*** Error: waitpid\n"); 
			}

			return; 
		}
		assert(ret > 0); 
	}
}

/* The definition of this function is in job.hh */ 
void job_print_jobs()
{
	for (auto &i:  Execution::executions_by_pid) {
		i.second->print_as_job(); 
	}
}

bool Execution::find_cycle(const Execution *const parent, 
			   const Execution *const child,
			   const Link &link)
{
	/* Happens when the parent is the root execution */ 
	if (parent->param_rule == nullptr)
		return false;
		
	/* Happens with files that should be there and have no rule */ 
	if (child->param_rule == nullptr)
		return false; 

	vector <const Execution *> path;
	path.push_back(parent); 

	return find_cycle(path, child, link); 
}

bool Execution::find_cycle(vector <const Execution *> &path,
			   const Execution *const child,
			   const Link &link)
{
	if (same_rule(path.back(), child)) {
		cycle_print(path, link); 
		return true; 
	}

	for (auto &i:  path.back()->parents) {
		const Execution *next= i.first; 
		assert(next != nullptr);
		if (next->param_rule == nullptr)
			continue;

		path.push_back(next); 

		bool found= find_cycle(path, child, link);
		if (found)
			return true;

		path.pop_back(); 
	}
	
	return false; 
}

bool Execution::remove_if_existing(bool output) 
{
	if (option_no_delete)
		return false;

	/* Whether anything was removed */ 
	bool removed= false;

	for (unsigned i= 0;  i < targets.size();  ++i) {
		const Target &target= targets[i]; 

		if (target.type != Type::FILE)  
			continue;

		const char *filename= target.name.c_str();

		/* Remove the file if it exists.  If it is a symlink, only the
		 * symlink itself is removed, not the file it links to */ 

		struct stat buf;
		if (0 > stat(filename, &buf))
			continue;

		/* If the file existed before building, remove it only if it now
		 * has a newer timestamp. 
		 */

		if (! timestamps_old[i].defined() ||
		    timestamps_old[i] < Timestamp(&buf)) {

			if (output) {
				print_info(frmt("Removing file %s%s%s because command failed",
						Color::beg_name_quoted, filename, Color::end_name_quoted)); 
			}
			
			removed= true;

			if (0 > ::unlink(filename)) {
				if (output) {
					print_error_system(filename); 
				} else {
					write_safe(2, "*** Error: unlink\n");
				}
			}
		}
	}

	return removed; 
}

Execution *Execution::get_execution(const Target &target, 
				    Link &&link,
				    Execution *parent)
{
	/* Set to the returned Execution object when one is found or created */    
	Execution *execution= nullptr; 

	auto it= executions_by_target.find(target);

	if (it != executions_by_target.end()) {
		/* An Execution object already exists for the target */ 

		execution= it->second; 
		if (execution->parents.count(parent)) {
			/* The parent and child are already connected -- add the
			 * necessary flags */ 
			execution->parents.at(parent).add(link.avoid, 
							  link.flags);
		} else {
			/* The parent and child are not connected -- add the
			 * connection */ 
			execution->parents[parent]= link;
		}
		
	} else { 
		/* Create a new Execution object */ 

		execution= new Execution(target, move(link), parent);  

		assert(execution->parents.size() == 1); 
	}

	if (find_cycle(parent, execution, link)) {
		parent->raise(ERROR_LOGICAL);
		return nullptr;
	}

	execution->initialize(link.avoid); 

	return execution;
}

void Execution::read_dynamics(Stack avoid,
			      shared_ptr <Dependency> dependency_this)
{
	Target target= dependency_this->get_single_target().unparametrized(); 

	assert(target.type.is_dynamic());

	assert(avoid.get_k() == target.type.get_dynamic_depth()); 

	try {
		vector <shared_ptr <Token> > tokens;
		const string filename= target.name; 
		Place place_end; 

		Parse::parse_tokens_file(tokens, 
					 Parse::DYNAMIC,
					 place_end, filename);

		vector <shared_ptr <Dependency> > dependencies;
		Place_Param_Name input; /* remains empty */ 
		Place place_input; /* remains empty */ 

		try {
			Build::get_expression_list(dependencies, tokens, 
						   place_end, input, place_input);
		} catch (int e) {
			raise(e); 
		}

		for (auto &j:  dependencies) {

			/* Check that it is unparametrized */ 
			if (! j->is_unparametrized()) {
				shared_ptr <Dependency> dep= j;
				while (dynamic_pointer_cast <Dynamic_Dependency> (dep)) {
					shared_ptr <Dynamic_Dependency> dep2= 
						dynamic_pointer_cast <Dynamic_Dependency> (dep);
					dep= dep2->dependency; 
				}
				dynamic_pointer_cast <Direct_Dependency> (dep)
					->place_param_target.place_param_name.places[0] <<
					fmt("dynamic dependency %s must not contain parametrized dependencies",
					    target.format_err());
				Target target_base= target;
				target_base.type= target.type.get_base();
				print_traces(fmt("%s is declared here", 
						 target_base.format_err())); 
				raise(ERROR_LOGICAL);
				continue; 
			}

			/* Check that there is no multiply-dynamic variable dependency */ 
			if (j->has_flags(F_VARIABLE) && 
			    target.type.is_dynamic() && target.type != Type::DYNAMIC_FILE) {
				
				/* Only direct dependencies can have the F_VARIABLE flag set */ 
				assert(dynamic_pointer_cast <Direct_Dependency> (j));

				shared_ptr <Direct_Dependency> dep= 
					dynamic_pointer_cast <Direct_Dependency> (j);

				j->get_place() <<
					fmt("variable dependency %s$[%s]%s must not appear",
					    Color::beg_name_bare, dep->place_param_target.format_mid(), Color::end_name_bare);
				print_traces(fmt("within multiply-dynamic dependency %s", 
						 target.format_err())); 
				raise(ERROR_LOGICAL);
				continue; 
			}

			/* Add the found dependencies, with one less dynamic level
			 * than the current target.  */

			shared_ptr <Dependency> dependency(j);

			vector <shared_ptr <Dynamic_Dependency> > vec;
			shared_ptr <Dependency> p= dependency_this;

			while (dynamic_pointer_cast <Dynamic_Dependency> (p)) {
				shared_ptr <Dynamic_Dependency> dynamic_dependency= 
					dynamic_pointer_cast <Dynamic_Dependency> (p);
				vec.resize(vec.size() + 1);
				vec[vec.size() - 1]= dynamic_dependency;
				p= dynamic_dependency->dependency;   
			}

			Stack avoid_this= avoid;
			assert(vec.size() == avoid_this.get_k());
			avoid_this.pop(); 
			dependency->add_flags(avoid_this.get_lowest()); 
			if (dependency->get_place_existence().empty())
				dependency->set_place_existence
					(vec[target.type.get_dynamic_depth() - 1]
					 ->get_place_existence()); 
			if (dependency->get_place_optional().empty())
				dependency->set_place_optional
					(vec[target.type.get_dynamic_depth() - 1]
					 ->get_place_optional()); 
			if (dependency->get_place_trivial().empty())
				dependency->set_place_trivial
					(vec[target.type.get_dynamic_depth() - 1]
					 ->get_place_trivial()); 

			for (Type k= target.type - 1;  k.is_dynamic();  --k) {
				avoid_this.pop(); 
				Flags flags_level= avoid_this.get_lowest(); 
				dependency= make_shared <Dynamic_Dependency> (flags_level, dependency); 
				dependency->set_place_existence
					(vec[k.get_dynamic_depth() - 1]->get_place_existence()); 
				dependency->set_place_optional
					(vec[k.get_dynamic_depth() - 1]->get_place_optional()); 
				dependency->set_place_trivial
					(vec[k.get_dynamic_depth() - 1]->get_place_trivial()); 
			}

			assert(avoid_this.get_k() == 0); 

			buf_default.push(Link(dependency));

			/* Check that there are no input dependencies */ 
			if (! input.empty()) {
				j->get_place() <<
					fmt("dynamic dependency %s must not contain input redirection", 
					    target.format_err());
				Target target_file= target;
				target_file.type= Type::FILE;
				print_traces(fmt("%s is declared here", target_file.format_err())); 
				raise(ERROR_LOGICAL);
				continue; 
			}
		}
				
	} catch (int e) {
		/* We catch not only the errors raise in this function,
		 * but also the errors raised in the parser.  
		 */
		raise(e); 
	}
}

void Execution::warn_future_file(struct stat *buf, const char *filename)
{
  	if (timestamp_last < Timestamp(buf)) {
		print_warning(fmt("File %s%s%s has modification time in the future",
				  Color::beg_name_quoted, filename, Color::end_name_quoted));
	}
}

void Execution::print_traces(string text) const
{	
	/* The following traverses the execution graph backwards until it
	 * finds the root. We always take the first found parent, which
	 * is an arbitrary choice, but it doesn't matter here *which*
	 * dependency path we point out as an error, so the first one
	 * it is.  
	 */

	const Execution *execution= this; 

	/* If the error happens directly for the root execution, it was
	 * an error on the command line; don't output anything beyond
	 * the error message. */
	if (execution->targets.empty())
		return;

	bool first= true; 

	/* If there is a rule for this target, show the message with the
	 * rule's trace, otherwise show the message with the first
	 * dependency trace */ 
	if (execution->param_rule != nullptr && text != "") {
		execution->param_rule->place << text;
		first= false;
	}

	string text_parent= parents.begin()->second.dependency
		->get_single_target().format_err();

	while (true) {

		auto i= execution->parents.begin(); 

		if (i->first->targets.empty()) {
			if (first && text != "") {
				if (output_mode > Output::SILENT)
					print_error(fmt("No rule to build %s", 
							text_parent)); 
			}
			break; 
		}

		string text_child= text_parent; 

		text_parent= i->first->parents.begin()->second.dependency
			->get_single_target().format_err();

		/* Don't show [[A]]->A edges */
		if (i->second.flags & F_READ) {
			execution= i->first; 
			continue;
		}

		Place place= i->second.place;

		string msg;
		if (first && text != "") {
			msg= fmt("%s, needed by %s", text, text_parent); 
			first= false;
		} else {	
			msg= fmt("%s is needed by %s",
				 text_child, text_parent);
		}
		place << msg;
		
		execution= i->first; 
	}
}

void Execution::print_command() const
{
	if (output_mode < Output::SHORT)
		return; 

	if (output_mode == Output::SHORT) {
		bool first= true; 
		for (const Target &target:  targets) {
			if (first) { 
				first= false;  
			} else
				putc(' ', stdout); 
			string text= target.format_mid();
			fputs(text.c_str(), stdout); 
		}
		putc('\n', stdout); 
		return;
	} 

	if (rule->is_hardcode) {
		assert(targets.size() == 1); 
		string text= targets.front().format_out(); 
		printf("Creating %s\n", text.c_str());
		return;
	} 

	if (rule->is_copy) {
		/* To the user, we hide the fact that we are using '--' */ 
		assert(rule->place_param_targets.size() == 1); 
		string cp_target= rule->place_param_targets[0]->place_param_name.format_out();
		string cp_source= rule->filename.format_out();
		printf("cp %s %s\n", cp_source.c_str(), cp_target.c_str()); 
		return; 
	}

	/* We are printing a regular command */

	if (option_individual)
		return; 

	/* For single-line commands, show the variables on the same line.
	 * For multi-line commands, show them on a separate line. */ 
	bool single_line= rule->command->get_lines().size() == 1;
	bool begin= true; 

	string filename_output= rule->redirect_index < 0 ? "" :
		rule->place_param_targets[rule->redirect_index]
		->place_param_name.unparametrized();
	string filename_input= rule->filename.unparametrized(); 

	/* Redirections */
	if (filename_output != "") {
		if (! begin)
			putchar(' '); 
		begin= false;
		printf(">%s", filename_output.c_str()); 
	}
	if (filename_input != "") {
		if (! begin)
			putchar(' '); 
		begin= false;
		printf("<%s", filename_input.c_str()); 
	}

	/* Print the parameter values.  (Variable assignments are not printed) 
	 */ 
	for (auto i= mapping_parameter.begin(); i != mapping_parameter.end();  ++i) {
		string name= i->first;
		string value= i->second;
		if (! begin)
			putchar(' '); 
		begin= false;
		printf("%s=%s", name.c_str(), value.c_str());
	}

	/* Colon */ 
	if (! begin) {
		if (! single_line) 
			puts(":"); 
		else
			fputs(": ", stdout);
	}

	/* The command itself */ 
	for (auto &i:  rule->command->get_lines()) {
		puts(i.c_str()); 
	}
}

bool Execution::deploy(const Link &link,
		       const Link &link_child)
{
	if (option_verbose) {
		string text_target= verbose_target();
		string text_link_child= link_child.format(); 
		fprintf(stderr, "VERBOSE %s %s deploy %s\n",
			Verbose::padding(),
			text_target.c_str(),
			text_link_child.c_str());
	}

	/* Additional flags for the child are added here */ 
	Flags flags_child= link_child.flags; 
	Flags flags_child_additional= 0; 

	int dynamic_depth= 0;
	shared_ptr <Dependency> dep= link_child.dependency;
	while (dynamic_pointer_cast <Dynamic_Dependency> (dep)) {
		dep= dynamic_pointer_cast <Dynamic_Dependency> (dep)->dependency;
		++dynamic_depth;
	}

	shared_ptr <Direct_Dependency> direct_dependency=
		dynamic_pointer_cast <Direct_Dependency> (dep);
	assert(! direct_dependency->place_param_target.place_param_name.empty()); 

	Target target_child= direct_dependency->place_param_target.unparametrized();
	assert(target_child.type == Type::FILE || target_child.type == Type::TRANSIENT);

	if (dynamic_depth != 0) {
		assert(dynamic_depth > 0);

		target_child.type += dynamic_depth; 
	}

	Stack avoid_child= link_child.avoid;

	/* Carry flags over transient targets */ 
	if (! targets.empty()) {
		Target target= link.dependency->get_single_target().unparametrized();

		if (target.type == Type::TRANSIENT) { 
			flags_child_additional |= link.flags; 
			avoid_child.add_highest(link.flags);
			if (link.flags & F_EXISTENCE) {
				link_child.dependency->set_place_existence
					(link.dependency->get_place_existence()); 
			}
			if (link.flags & F_OPTIONAL) {
				link_child.dependency->set_place_optional
					(link.dependency->get_place_optional()); 
			}
			if (link.flags & F_TRIVIAL) {
				link_child.dependency->set_place_trivial
					(link.dependency->get_place_trivial()); 
			}
		}
	}
	
	Flags flags_child_new= flags_child | flags_child_additional; 

	/* '!' and '?' do not mix, even for old flags */ 
	if ((flags_child_new & F_EXISTENCE) && 
	    (flags_child_new & F_OPTIONAL)) {

		/* '!' and '?' encountered for the same target */ 

		const Place &place_existence= 
			link_child.dependency->get_place_existence();
		const Place &place_optional= 
			link_child.dependency->get_place_optional();
		place_existence <<
			fmt("declaration of existence-only dependency with %s",
			     char_format_err('!')); 
		place_optional <<
			fmt("clashes with declaration of optional dependency with %s",
			     char_format_err('?')); 
		direct_dependency->place <<
			fmt("in declaration of dependency %s", 
			    target_child.format_err());
		print_traces();
		explain_clash(); 
		raise(ERROR_LOGICAL);
		return false;
	}

	/* Either of '!'/'?'/'&' does not mix with '$[' */
	if ((flags_child & F_VARIABLE) &&
	    (flags_child_additional & (F_EXISTENCE | F_OPTIONAL | F_TRIVIAL))) {

		const Place &place_variable= direct_dependency->place;
		if (flags_child_additional & F_EXISTENCE) {
			const Place &place_flag= link_child.dependency->get_place_existence(); 
			place_variable << fmt("variable dependency %s$[%s]%s must not be declared as existence-only dependency",
					      Color::beg_name_bare, target_child.format_mid(), Color::end_name_bare);
			place_flag << fmt("using %s",
					   char_format_err('!')); 
		} else if (flags_child_additional & F_OPTIONAL) {
			const Place &place_flag= link_child.dependency->get_place_optional(); 
			place_variable << fmt("variable dependency %s$[%s]%s must not be declared as optional dependency",
					      Color::beg_name_bare, target_child.format_mid(), Color::end_name_bare);
			place_flag << fmt("using %s",
					   char_format_err('?')); 
		} else {
			assert(flags_child_additional & F_TRIVIAL); 
			const Place &place_flag= link_child.dependency->get_place_trivial(); 
			place_variable << fmt("variable dependency %s$[%s]%s must not be declared as trivial dependency",
					      Color::beg_name_bare, target_child.format_mid(), Color::end_name_bare);
			place_flag << fmt("using %s",
					   char_format_err('&')); 
		} 
		print_traces();
		raise(ERROR_LOGICAL);
		return false;
	}

	flags_child= flags_child_new; 

	Execution *child= Execution::get_execution
		(target_child, 
		 Link(avoid_child,
		      flags_child,
		      direct_dependency->place,
		      link_child.dependency),
		 this);  
	if (child == nullptr) {
		/* Strong cycle was found */ 
		return false;
	}

	children.insert(child);

	Link link_child_new(avoid_child, flags_child, link_child.place, link_child.dependency); 
	
	if (child->execute(this, move(link_child_new)))
		return true;
	assert(jobs >= 0);
	if (jobs == 0)  
		return false;
			
	if (child->finished(avoid_child)) {
		unlink(this, child, 
		       link.dependency,
		       link.avoid, 
		       link_child.dependency,
		       avoid_child, flags_child);
	}

	return false;
}

void Execution::initialize(Stack avoid) 
{
	if (! targets.empty() &&
	    targets.front().type.is_dynamic()) {

		assert(targets.size() == 1); 
		Target target= targets.front(); 

		/* This is a special dynamic target.  Add, as an initial
		 * dependency, the corresponding file or transient.  
		 */
		Flags flags_child= avoid.get_lowest();
		
		if (target.type.is_any_file())
			flags_child |= F_READ;

		shared_ptr <Dependency> dependency_child= make_shared <Direct_Dependency>
			(flags_child,
			 Place_Param_Target(target.type.get_base(), Place_Param_Name(target.name)));

		buf_default.push(Link(dependency_child, flags_child, Place()));
		/* The place of the [[A]]->A links is empty, meaning it will
		 * not be output in traces. */ 
	} 
}

void Execution::print_as_job() const
{
	pid_t pid= job.get_pid();

	string text_target= targets.front().format_out(); 

	printf("%7u %s\n", (unsigned) pid, text_target.c_str());
}

void Execution::write_content(const char *filename, 
			      const Command &command)
{
	FILE *file= fopen(filename, "w"); 

	if (file == nullptr) {
		print_error_system(filename);
		if (output_mode > Output::SILENT)
			command.get_place() << 
				fmt("error creating %s", 
				    name_format_err(filename)); 
		raise(ERROR_BUILD); 
	}

	for (const string &line:  command.get_lines()) {
		if (fwrite(line.c_str(), 1, line.size(), file) != line.size()) {
			assert(ferror(file));
			print_error_system(filename);
			raise(ERROR_BUILD); 
		}
		if (EOF == putc('\n', file)) {
			print_error_system(filename);
			raise(ERROR_BUILD); 
		}
	}

	if (0 != fclose(file)) {
		print_error_system(filename);
		if (output_mode > Output::SILENT)
			command.get_place() << 
				fmt("error creating %s", 
				     name_format_err(filename)); 
		raise(ERROR_BUILD); 
	}

	exists= +1;
}

bool Execution::read_variable(string &variable_name,
			      string &content,
			      shared_ptr <Dependency> dependency)
{
	Target target= dependency->get_single_target().unparametrized(); 

	assert(target.type == Type::FILE);

	size_t filesize;
	struct stat buf;
	string dependency_variable_name;
	
	int fd= open(target.name.c_str(), O_RDONLY);
	if (fd < 0) {
		if (errno != ENOENT)
			print_error_system(target.name.c_str()); 
		goto error;
	}
	if (0 > fstat(fd, &buf)) {
		print_error_system(target.name.c_str()); 
		goto error_fd;
	}

	filesize= buf.st_size;
	content.resize(filesize);
	if ((ssize_t)filesize != read(fd, (void *) content.c_str(), filesize)) {
		print_error_system(target.name.c_str()); 
		goto error_fd;
	}

	if (0 > close(fd)) { 
		print_error_system(target.name.c_str()); 
		goto error;
	}

	/* Remove space at beginning and end of the content.
	 * The characters are exactly those used by isspace() in
	 * the C locale.  
	 */ 
	content.erase(0, content.find_first_not_of(" \n\t\f\r\v")); 
	content.erase(content.find_last_not_of(" \n\t\f\r\v") + 1);  

	/* The variable name */ 
	dependency_variable_name=
		dynamic_pointer_cast <Direct_Dependency> (dependency)->name; 

	variable_name= 
		dependency_variable_name == "" ?
		target.name : dependency_variable_name;

	return true;

	error_fd:
		close(fd); 
	error:
		assert(dynamic_pointer_cast <Direct_Dependency> (dependency)); 
		Target target_variable= 
			dynamic_pointer_cast <Direct_Dependency> (dependency)->place_param_target
			.unparametrized(); 

		if (rule == nullptr) {
			dependency->get_place() <<
				fmt("file %s was up to date but cannot be found now", 
				    target_variable.format_err());
		} else {
			for (auto const &place_param_target: rule->place_param_targets) {
				if (place_param_target->unparametrized() == target_variable) {
					place_param_target->place <<
						fmt("generated file %s was built but cannot be found now", 
						    place_param_target->format_err());
					break;
				}
			}
		}
		print_traces();

		raise(ERROR_BUILD); 
		
		return false;
}

void Execution::cycle_print(const vector <const Execution *> &path,
			    const Link &link)
{
	assert(path.size() > 0); 

	/* Indexes are parallel to PATH */ 
	vector <string> names;
	names.resize(path.size());
	
	for (unsigned i= 0;  i + 1 < path.size();  ++i) {
		names[i]= 
			path[i]->parents.at((Execution *)path[i+1])
			.dependency->get_single_target().format_err();
	}

	names.back()= path.back()->parents.begin()->second
		.dependency->get_single_target().format_err();
		
	for (signed i= path.size() - 1;  i >= 0;  --i) {

		/* Don't show a message for [...[A]...] -> X links */ 
		if (i != 0 &&
		    path[i - 1]->parents.at((Execution *)path[i])
		    .dependency->get_flags() & F_READ)
			continue;

		/* Same, but when [...[A]...] is at the bottom */
		if (i == 0 && link.dependency->get_flags() & F_READ) 
			continue;

		(i == 0 ? link : path[i - 1]->parents.at((Execution *)path[i]))
			.place
			<< fmt("%s%s depends on %s",
			       i == (int)(path.size() - 1) 
			       ? (path.size() == 1 
				  || (path.size() == 2 &&
				      link.dependency->get_flags() & F_READ)
				  ? "target must not depend on itself: " 
				  : "cyclic dependency: ") 
			       : "",
			       names[i],
			       i == 0
			       ? link.dependency->get_single_target().format_err()
			       : names[i - 1]);
	}

	/* If the two targets are different (but have the same rule
	 * because they match the same pattern), then output a notice to
	 * that effect */ 
	if (link.dependency->get_single_target() !=
	    path.back()->parents.begin()->second
	    .dependency->get_single_target()) {

		
		Param_Target t1= path.back()->parents.begin()->second
			.dependency->get_single_target();
		Param_Target t2= link.dependency->get_single_target();
		t1.type= t1.type.get_base();
		t2.type= t2.type.get_base(); 

		path.back()
			->rule->place
			<< fmt("both %s and %s match the same rule",
			       t1.format_err(), t2.format_err());
	}

	path.back()->print_traces();

	explain_cycle(); 
}

bool Execution::same_rule(const Execution *execution_a,
			  const Execution *execution_b)
{
	return 
		execution_a->param_rule != nullptr &&
		execution_b->param_rule != nullptr &&
		execution_a->get_dynamic_depth() == execution_b->get_dynamic_depth() &&
		execution_a->param_rule == execution_b->param_rule;
}

void Execution::raise(int error_) {
	assert(error_ >= 1 && error_ <= 3); 
	error |= error_;
	if (! option_keep_going)
		throw error;
}

bool Execution::is_dynamic() const {
	return targets.size() && targets.front().type.is_dynamic(); 
}

#endif /* ! EXECUTION_HH */
