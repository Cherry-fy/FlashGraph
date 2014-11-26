/**
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of FlashGraph.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <signal.h>
#ifdef PROFILER
#include <gperftools/profiler.h>
#endif

#include <vector>

#include "graph_engine.h"
#include "graph_config.h"
#include "FGlib.h"

using namespace safs;
using namespace fg;

atomic_number<long> num_visits;
edge_type traverse_edge = edge_type::OUT_EDGE;

class dist_message: public vertex_message
{
	int parent_dist;
	vertex_id_t parent;
public:
	dist_message(vertex_id_t parent, int parent_dist): vertex_message(
			sizeof(dist_message), true) {
		this->parent = parent;
		this->parent_dist = parent_dist;
	}

	vertex_id_t get_parent() const {
		return parent;
	}

	int get_parent_dist() const {
		return parent_dist;
	}
};

class sssp_vertex: public compute_directed_vertex
{
	int parent_dist;
	vertex_id_t tmp_parent;
	int distance;
	vertex_id_t parent;
public:
	sssp_vertex(vertex_id_t id): compute_directed_vertex(id) {
		parent_dist = INT_MAX;
		tmp_parent = -1;
		distance = INT_MAX;
		parent = -1;
	}

	void init(int distance) {
		this->distance = distance;
		parent = -1;
	}

	void run(vertex_program &prog) {
		if (parent_dist + 1 < distance) {
			distance = parent_dist + 1;
			parent = tmp_parent;

			directed_vertex_request req(prog.get_vertex_id(*this),
					traverse_edge);
			request_partial_vertices(&req, 1);
		}
	}

	void run(vertex_program &prog, const page_vertex &vertex);

	void run_on_message(vertex_program &, const vertex_message &msg1) {
		const dist_message &msg = (const dist_message &) msg1;
		if (parent_dist > msg.get_parent_dist()) {
			parent_dist = msg.get_parent_dist();
			tmp_parent = msg.get_parent();
		}
	}
};

void sssp_vertex::run(vertex_program &prog, const page_vertex &vertex)
{
#ifdef DEBUG
	num_visits.inc(1);
#endif
	int num_dests = vertex.get_num_edges(traverse_edge);
	if (num_dests == 0)
		return;

	// We need to add the neighbors of the vertex to the queue of
	// the next level.
	dist_message msg(prog.get_vertex_id(*this), distance);
	if (traverse_edge == BOTH_EDGES) {
		edge_seq_iterator it = vertex.get_neigh_seq_it(IN_EDGE, 0,
				num_dests);
		prog.multicast_msg(it, msg);
		it = vertex.get_neigh_seq_it(OUT_EDGE, 0, num_dests);
		prog.multicast_msg(it, msg);
	}
	else {
		edge_seq_iterator it = vertex.get_neigh_seq_it(traverse_edge, 0,
				num_dests);
		prog.multicast_msg(it, msg);
	}
}

class sssp_initializer: public vertex_initializer
{
public:
	virtual void init(compute_vertex &v) {
		sssp_vertex &sv = (sssp_vertex &) v;
		sv.init(0);
	}
};

void int_handler(int sig_num)
{
#ifdef PROFILER
	if (!graph_conf.get_prof_file().empty())
		ProfilerStop();
#endif
	exit(0);
}

void print_usage()
{
	fprintf(stderr,
			"sssp [options] conf_file graph_file index_file start_vertex\n");
	fprintf(stderr, "-c confs: add more configurations to the system\n");
	fprintf(stderr, "-b: traverse with both in-edges and out-edges\n");
	graph_conf.print_help();
	params.print_help();
}

int main(int argc, char *argv[])
{
	int opt;
	std::string confs;
	int num_opts = 0;
	while ((opt = getopt(argc, argv, "c:b")) != -1) {
		num_opts++;
		switch (opt) {
			case 'c':
				confs = optarg;
				num_opts++;
				break;
			case 'b':
				traverse_edge = edge_type::BOTH_EDGES;
				break;
			default:
				print_usage();
		}
	}
	argv += 1 + num_opts;
	argc -= 1 + num_opts;

	if (argc < 4) {
		print_usage();
		exit(-1);
	}

	std::string conf_file = argv[0];
	std::string graph_file = argv[1];
	std::string index_file = argv[2];
	vertex_id_t start_vertex = atoi(argv[3]);

	config_map::ptr configs = config_map::create(conf_file);
	assert(configs);
	configs->add_options(confs);

	signal(SIGINT, int_handler);

	FG_graph::ptr fg = FG_graph::create(graph_file, index_file, configs);
	graph_index::ptr index = NUMA_graph_index<sssp_vertex>::create(
			fg->get_graph_header());
	graph_engine::ptr graph = fg->create_engine(index);
	printf("SSSP starts\n");
	printf("prof_file: %s\n", graph_conf.get_prof_file().c_str());
#ifdef PROFILER
	if (!graph_conf.get_prof_file().empty())
		ProfilerStart(graph_conf.get_prof_file().c_str());
#endif

	struct timeval start, end;
	gettimeofday(&start, NULL);
	graph->start(&start_vertex, 1, vertex_initializer::ptr(
				new sssp_initializer()));
	graph->wait4complete();
	gettimeofday(&end, NULL);

#ifdef PROFILER
	if (!graph_conf.get_prof_file().empty())
		ProfilerStop();
#endif
	printf("SSSP starts from vertex %ld. It takes %f seconds\n",
			(unsigned long) start_vertex, time_diff(start, end));
#ifdef DEBUG
	printf("%ld vertices are visited\n", num_visits.get());
#endif
}
