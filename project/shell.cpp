/**
	* Shell
	* Operating Systems
	* v20.08.28
	*/

/**
	Hint: Control-click on a functionname to go to the definition
	Hint: Ctrl-space to auto complete functions and variables
	*/

// function/class definitions you are going to use
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>

#include <vector>

// although it is good habit, you don't have to type 'std' before many objects by including this line
using namespace std;

struct Command {
	vector<string> parts = {};
};

struct Expression {
	vector<Command> commands;
	string inputFromFile;
	string outputToFile;
	bool background = false;
};

#define READ_END 0
#define WRITE_END 1

// Parses a string to form a vector of arguments. The seperator is a space char (' ').
vector<string> splitString(const string& str, char delimiter = ' ') {
	vector<string> retval;
	for (size_t pos = 0; pos < str.length(); ) {
		// look for the next space
		size_t found = str.find(delimiter, pos);
		// if no space was found, this is the last word
		if (found == string::npos) {
			retval.push_back(str.substr(pos));
			break;
		}
		// filter out consequetive spaces
		if (found != pos)
			retval.push_back(str.substr(pos, found-pos));
		pos = found+1;
	}
	return retval;
}

// wrapper around the C execvp so it can be called with C++ strings (easier to work with)
// always start with the command itself
// always terminate with a NULL pointer
// DO NOT CHANGE THIS FUNCTION UNDER ANY CIRCUMSTANCE
int execvp(const vector<string>& args) {
	// build argument list
	const char** c_args = new const char*[args.size()+1];
	for (size_t i = 0; i < args.size(); ++i) {
		c_args[i] = args[i].c_str();
	}
	c_args[args.size()] = nullptr;
	// replace current process with new process as specified
	::execvp(c_args[0], const_cast<char**>(c_args));
	// if we got this far, there must be an error
	int retval = errno;
	// in case of failure, clean up memory
	delete[] c_args;
	return retval;
}

// Executes a command with arguments. In case of failure, returns error code.
int executeCommand(const Command& cmd) {
	auto& parts = cmd.parts;
	if (parts.size() == 0)
		return EINVAL;

	// execute external commands
	int retval = execvp(parts);
	return retval;
}

void displayPrompt() {
	char buffer[512];
	char* dir = getcwd(buffer, sizeof(buffer));
	if (dir) {
		cout << "\e[32m" << dir << "\e[39m"; // the strings starting with '\e' are escape codes, that the terminal application interpets in this case as "set color to green"/"set color to default"
	}
	cout << "$ ";
	flush(cout);
}

string requestCommandLine(bool showPrompt) {
	if (showPrompt) {
		displayPrompt();
	}
	string retval;
	getline(cin, retval);
	return retval;
}

// note: For such a simple shell, there is little need for a full blown parser (as in an LL or LR capable parser).
// Here, the user input can be parsed using the following approach.
// First, divide the input into the distinct commands (as they can be chained, separated by `|`).
// Next, these commands are parsed separately. The first command is checked for the `<` operator, and the last command for the `>` operator.
Expression parseCommandLine(string commandLine) {
	Expression expression;
	vector<string> commands = splitString(commandLine, '|');
	for (size_t i = 0; i < commands.size(); ++i) {
		string& line = commands[i];
		vector<string> args = splitString(line, ' ');
		if (i == commands.size() - 1 && args.size() > 1 && args[args.size()-1] == "&") {
			expression.background = true;
			args.resize(args.size()-1);
		}
		if (i == commands.size() - 1 && args.size() > 2 && args[args.size()-2] == ">") {
			expression.outputToFile = args[args.size()-1];
			args.resize(args.size()-2);
		}
		if (i == 0 && args.size() > 2 && args[args.size()-2] == "<") {
			expression.inputFromFile = args[args.size()-1];
			args.resize(args.size()-2);
		}
		expression.commands.push_back({args});
	}
	return expression;
}

int execCmd(Expression& expression) {
	int pid = fork();
	if(pid == 0) {
		if (expression.background) {
			close(STDIN_FILENO);
		}
		if (strcmp(expression.outputToFile.c_str(), "") != 0) {
			int filefd = open(expression.outputToFile.c_str(), O_CREAT | O_RDWR, 0444);
			dup2(filefd, STDOUT_FILENO);
			close(filefd);
		} else if (strcmp(expression.inputFromFile.c_str(), "") != 0) {
			freopen(expression.inputFromFile.c_str(), "r", stdin);
		}
		executeCommand(expression.commands.at(0));
	}
	if (expression.background == false) {
		waitpid(pid, nullptr, 0);
	}
	return 0;
}

int executeExpression(Expression& expression) {
	// Check for empty expression
	if (expression.commands.size() == 0)
		return EINVAL;

	// Handle intern commands (like 'cd' and 'exit')
	if (strcmp(expression.commands.at(0).parts.at(0).c_str(), "exit") == 0) {
		exit(0);
	}
	if (strcmp(expression.commands.at(0).parts.at(0).c_str(), "cd") == 0)
		chdir(expression.commands.at(0).parts.at(1).c_str());

	// External commands, executed with fork():
	// Loop over all commandos, and connect the output and input of the forked processes
	if (expression.commands.size() < 2) {
		execCmd(expression);
	} else {
		int nCommand = expression.commands.size();
		int pipefds[nCommand-1][2];
		int pids[nCommand];

		for(int i=0; i<nCommand-1; i++){
			pipe(pipefds[i]);
		}

		for(int i = 0; i < nCommand; i++) {
			pids[i] = fork();
			if(pids[i] == 0) {
				if(i == 0) { // First command
					if (expression.background) {
						close(STDIN_FILENO);
					}
					if (strcmp(expression.inputFromFile.c_str(), "") != 0) {
						freopen(expression.inputFromFile.c_str(), "r", stdin);
						dup2(pipefds[i][WRITE_END], STDOUT_FILENO);
						close(pipefds[i][READ_END]);
						close(pipefds[i][WRITE_END]);
					} else {
						dup2(pipefds[i][WRITE_END], STDOUT_FILENO);
						close(pipefds[i][READ_END]);
						close(pipefds[i][WRITE_END]);
					}
				} 
				else if (i+1 == nCommand) { // Last command
					if (strcmp(expression.outputToFile.c_str(), "") != 0) {
						int filefd = open(expression.outputToFile.c_str(), O_CREAT | O_RDWR, 0444);
						dup2(pipefds[i-1][READ_END], STDIN_FILENO);
						dup2(filefd, STDOUT_FILENO);
						close(pipefds[i-1][WRITE_END]);
						close(pipefds[i-1][READ_END]);
						close(filefd);
					} else {
						dup2(pipefds[i-1][READ_END], STDIN_FILENO);
						close(pipefds[i-1][WRITE_END]);
						close(pipefds[i-1][READ_END]);
					}
				} 
				else { // Middle command(s)
					dup2(pipefds[i-1][READ_END], STDIN_FILENO);
					dup2(pipefds[i][WRITE_END], STDOUT_FILENO);
					close(pipefds[i-1][READ_END]);
					close(pipefds[i-1][WRITE_END]);
					close(pipefds[i][READ_END]);
					close(pipefds[i][WRITE_END]);
				}
				executeCommand(expression.commands.at(i));
				_exit(0);
				abort();
			}
		}
		
		for(int i=0; i<nCommand-1; i++){
			close(pipefds[i][READ_END]);
			close(pipefds[i][WRITE_END]);
		}
		if (expression.background == false) {
			for (int i = 0; i < nCommand; i++) {
				waitpid(pids[i], NULL, 0);
			}
		}
		
	}
	return 0;
}

int normal(bool showPrompt) {
	while (cin.good()) {
		string commandLine = requestCommandLine(showPrompt);
		Expression expression = parseCommandLine(commandLine);
		int rc = executeExpression(expression);
		if (rc != 0)
			cerr << strerror(rc) << endl;
	}
	return 0;
}

// framework for executing "date | tail -c 10" using raw commands
// two processes are created, and connected to each other
int step1(bool showPrompt) {
	int pipefd[2];
	// create communication channel shared between the two processes
	// ...
	if(pipe(pipefd) == -1) {
		fprintf(stderr, "Pipe failed");
		return 1;
	}

	pid_t child1 = fork();
	if (child1 == 0) {
		// redirect standard output (STDOUT_FILENO) to the input of the shared communication channel
		dup2(pipefd[WRITE_END], STDOUT_FILENO);
		// free non used resources (why?)
		//write(pipefd[WRITE_END], "testmessage", strlen("testmessage"));
		
		Command cmd = {{string("date")}};
		executeCommand(cmd);
		
		// display nice warning that the executable could not be found
		abort(); // if the executable is not found, we should abort. (why?)
	}

	pid_t child2 = fork();
	if (child2 == 0) {
		// redirect the output of the shared communication channel to the standard input (STDIN_FILENO).
		dup2(pipefd[READ_END], STDIN_FILENO);
		close(pipefd[WRITE_END]);

		// free non used resources (why?)
		Command cmd = {{string("tail"), string("-c"), string("5")}};
		executeCommand(cmd);
		abort(); // if the executable is not found, the child process should stop (to avoid having two shells active)
	}

	// free non used resources (why?)
	// wait on child processes to finish (why both?)
	waitpid(child1, nullptr, 0);
	waitpid(child2, nullptr, 0);
	return 0;
}

int shell(bool showPrompt) {
	return normal(showPrompt);
	//return step1(showPrompt);
	
}
