//  nbudstee
//
//  WEBSITE: https://bitbucket.org/JGRennison/nbudstee
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <vector>
#include <memory>
#include <deque>
#include <string>

enum class FDTYPE {
	NONE,
	STDIN,
	LISTENER,
	CONN,
};

struct fdinfo {
	FDTYPE type = FDTYPE::NONE;
	unsigned int pollfd_offset;
	std::string name;
	std::deque<std::shared_ptr<std::vector<unsigned char> > > out_buffers;
	size_t buffered_data = 0;
	bool have_overflowed = false;
	void clear() {
		out_buffers.clear();
	}
};

bool force_exit = false;
bool use_stdout = true;
size_t max_queue = 65536;
bool remove_after = false;
bool remove_before = false;
std::vector<struct pollfd> pollfds;
std::deque<struct fdinfo> fdinfos;

const size_t buffer_count_shrink_threshold = 4;

void addpollfd(int fd, short events, FDTYPE type, std::string name) {
	if(fdinfos.size() <= (size_t) fd) fdinfos.resize(fd + 1);
	if(fdinfos[fd].type != FDTYPE::NONE) {
		fprintf(stderr, "Attempt to add duplicate fd to poll array detected, ignoring: fd: %d\n", fd);
		return;
	}
	fdinfos[fd].type = type;
	fdinfos[fd].pollfd_offset = pollfds.size();
	fdinfos[fd].name = std::move(name);

	pollfds.push_back({ fd, events, 0 });
}

void delpollfd(int fd) {
	if((size_t) fd >= fdinfos.size() || fdinfos[fd].type == FDTYPE::NONE) {
		fprintf(stderr, "Attempt to remove non-existant fd from poll array detected, ignoring: fd: %d\n", fd);
		return;
	}

	size_t offset = fdinfos[fd].pollfd_offset;
	//offset is poll slot of fd currently being removed

	//if slot is not the last one, move the last one in to fill empty slot
	if(offset < pollfds.size() - 1) {
		pollfds[offset] = std::move(pollfds.back());
		int new_fd_in_slot = pollfds[offset].fd;
		fdinfos[new_fd_in_slot].pollfd_offset = offset;
	}
	pollfds.pop_back();
	fdinfos[fd].type = FDTYPE::NONE;
	fdinfos[fd].clear();
}

void setpollfdevents(int fd, short events) {
	size_t offset = fdinfos[fd].pollfd_offset;
	pollfds[offset].events = events;
}

void setnonblock(int fd, const char *name) {
	int flags = fcntl(fd, F_GETFL, 0);
	int res = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if(flags < 0 || res < 0) {
		fprintf(stderr, "Could not fcntl set O_NONBLOCK %s: %m\n", name);
		exit(1);
	}
}

std::deque<std::shared_ptr<std::vector<unsigned char> > > free_buffers;

std::shared_ptr<std::vector<unsigned char> > getbuffer() {
	if(free_buffers.empty()) return std::make_shared<std::vector<unsigned char> >();
	else {
		std::shared_ptr<std::vector<unsigned char> > buffer = std::move(free_buffers.back());
		free_buffers.pop_back();
		return std::move(buffer);
	}
}

void finished_with_buffer(std::shared_ptr<std::vector<unsigned char> > buffer) {
	if(buffer.unique()) {
		free_buffers.emplace_back(std::move(buffer));
	}
}

void cleanup() {
	if(remove_after) {
		for(auto &it : fdinfos) {
			if(it.type == FDTYPE::LISTENER) {
				unlink(it.name.c_str());
			}
		}
	}
}

std::shared_ptr<std::vector<unsigned char> > read_input_fd(int fd) {
	std::shared_ptr<std::vector<unsigned char> > buffer = getbuffer();
	buffer->resize(4096);
	ssize_t bread = read(fd, buffer->data(), buffer->size());
	if(bread < 0) {
		fprintf(stderr, "empty/failed read: %m\n");
		cleanup();
		exit(1);
	}
	else if(bread == 0) {
		cleanup();
		exit(0);
	}
	else if(bread > 0) {
		buffer->resize(bread);
		for(int fd = 0; fd < (int) fdinfos.size(); fd++) {
			if(fdinfos[fd].type == FDTYPE::CONN) {
				if(fdinfos[fd].buffered_data < max_queue) {
					fdinfos[fd].out_buffers.emplace_back(buffer);
					if(fdinfos[fd].out_buffers.size() >= buffer_count_shrink_threshold) {
						//Starting to accumulate a lot of buffers
						//Shrink to fit the older ones to avoid storing large numbers of potentially mostly empty buffers
						fdinfos[fd].out_buffers[fdinfos[fd].out_buffers.size() - buffer_count_shrink_threshold]->shrink_to_fit();
					}
					fdinfos[fd].buffered_data += buffer->size();
					setpollfdevents(fd, POLLOUT | POLLERR);
				}
				else if(!fdinfos[fd].have_overflowed) {
					fdinfos[fd].have_overflowed = true;
					fprintf(stderr, "Queue overflow for output: %s\n", fdinfos[fd].name.c_str());
				}
			}
		}
	}
	return std::move(buffer);
}

void sighandler(int sig) {
	force_exit = true;
}

static struct option options[] = {
	{ "help",          no_argument,        NULL, 'h' },
	{ "no-stdout",     no_argument,        NULL, 'n' },
	{ "unlink-after",  no_argument,        NULL, 'u' },
	{ "unlink-before", no_argument,        NULL, 'b' },
	{ "max-queue",     required_argument,  NULL, 'm' },
	{ NULL, 0, 0, 0 }
};

int main(int argc, char **argv) {
	int n = 0;
	while (n >= 0) {
		n = getopt_long(argc, argv, "hnubm:", options, NULL);
		if (n < 0) continue;
		switch (n) {
		case 'n':
			use_stdout = false;
			break;
		case 'u':
			remove_after = true;
			break;
		case 'b':
			remove_before = true;
			break;
		case 'm': {
			char *end = 0;
			max_queue = strtoul(optarg, &end, 0);
			if(!end) { /* do nothing*/ }
			else if(!*end) { /* valid integer */ }
			else if(end == std::string("k")) max_queue <<= 10;
			else if(end == std::string("M")) max_queue <<= 20;
			else if(end == std::string("G")) max_queue <<= 30;
			else {
				fprintf(stderr, "Invalid max queue length: '%s'\n", optarg);
			}
			break;
		}
		case '?':
		case 'h':
			fprintf(stderr,
					"Usage: nbudstee [options] uds1 uds2 ...\n"
					"\tCopy STDIN to zero or more non-blocking Unix domain sockets\n"
					"\teach of which can have zero or more connected readers.\n"
					"\tAlso copies to STDOUT, unless -n/--no-stdout is used.\n"
					"Options:\n"
					"-n, --no-stdout\n"
					"\tDo not copy input to STDOUT.\n"
					"-b, --unlink-before\n"
					"\tFirst try to unlink any existing sockets. This will not try to unlink non-sockets.\n"
					"-u, --unlink-after\n"
					"\tTry to unlink all sockets when done.\n"
					"-m, --max-queue <bytes>\n"
					"\tMaximum amount of data to buffer for each connected socket reader (approximate)\n"
					"\tAccepts suffixes: k, M, G, for multiples of 1024. Default: 64k\n"
					"\tAbove this limit new data for that socket reader will be discarded.\n"
					"Note:\n"
					"\tNo attempt is made to line-buffer or coalesce the input.\n"
			);
			exit(1);
		}
	}

	struct sigaction new_action;
	memset(&new_action, 0, sizeof(new_action));
	new_action.sa_handler = sighandler;
	sigaction(SIGINT, &new_action, 0);
	sigaction(SIGHUP, &new_action, 0);
	sigaction(SIGTERM, &new_action, 0);
	new_action.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &new_action, 0);

	setnonblock(STDIN_FILENO, "STDIN");
	addpollfd(STDIN_FILENO, POLLIN | POLLERR, FDTYPE::STDIN, "STDIN");

	while (optind < argc) {
		const char *name = argv[optind++];
		int sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if(sock == -1) {
			fprintf(stderr, "socket() failed, %m\n");
			continue;
		}
		struct sockaddr_un my_addr;
		memset(&my_addr, 0, sizeof(my_addr));
		my_addr.sun_family = AF_UNIX;
		size_t maxlen = sizeof(my_addr.sun_path) - 1;
		if(strlen(name) > maxlen) {
			fprintf(stderr, "Socket name: %s too long, maximum: %zu\n", name, maxlen);
			exit(1);
		}
		strncpy(my_addr.sun_path, name, maxlen);

		if(remove_before) {
			struct stat sb;
			if(stat(name, &sb) != -1) {
				if(S_ISSOCK(sb.st_mode)) {
					//only try to unlink if the existing file is a socket
					unlink(name);
				}
			}
		}

		if(bind(sock, (struct sockaddr *) &my_addr, sizeof(my_addr)) == -1) {
			fprintf(stderr, "bind(%s) failed, %m\n", name);
			continue;
		}

		if(listen(sock, 64) == -1) {
			fprintf(stderr, "listen(%s) failed, %m\n", name);
			continue;
		}

		setnonblock(sock, name);
		addpollfd(sock, POLLIN | POLLERR, FDTYPE::LISTENER, name);
	}

	while(!force_exit) {
		int n = poll(pollfds.data(), pollfds.size(), -1);
		if(n < 0) break;

		for(size_t i = 0; i < pollfds.size(); i++) {
			if(!pollfds[i].revents) continue;
			int fd = pollfds[i].fd;
			switch(fdinfos[fd].type) {
				case FDTYPE::NONE:
					exit(2);
				case FDTYPE::STDIN: {
					auto buffer = read_input_fd(fd);
					if(use_stdout) {
						ssize_t result = write(STDOUT_FILENO, buffer->data(), buffer->size());
						if(result < (ssize_t) buffer->size()) {
							fprintf(stderr, "Write to STDOUT failed/incomplete, wrote %zd instead of %zu, %m. Exiting.\n", result, buffer->size());
							cleanup();
							exit(1);
						}
					}
					finished_with_buffer(std::move(buffer));
					break;
				}
				case FDTYPE::LISTENER: {
					int newsock = accept(fd, 0, 0);
					if(newsock == -1) {
						fprintf(stderr, "accept(%s) failed, %m\n", fdinfos[fd].name.c_str());
						cleanup();
						exit(1);
					}
					setnonblock(newsock, fdinfos[fd].name.c_str());
					addpollfd(newsock, POLLERR, FDTYPE::CONN, fdinfos[fd].name);
					break;
				}
				case FDTYPE::CONN: {
					auto &out_buffers = fdinfos[fd].out_buffers;
					if(out_buffers.empty()) continue;
					auto buffer = std::move(out_buffers.front());
					out_buffers.pop_front();
					fdinfos[fd].buffered_data -= buffer->size();
					ssize_t result = write(fd, buffer->data(), buffer->size());
					if(result < (ssize_t) buffer->size()) {
						if(errno != EPIPE) {
							fprintf(stderr, "Write to %s failed/incomplete, wrote %zd instead of %zu, %m. Closing.\n", fdinfos[fd].name.c_str(), result, buffer->size());
						}
						close(fd);
						delpollfd(fd);
					}
					finished_with_buffer(std::move(buffer));
					break;
				}
			}
		}
	}
	cleanup();
	return 0;
}
