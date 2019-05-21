// $Id: opf_executive.h$
//	Copyright (C) 2017 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)
#ifndef _OPF_EXECUTIVE_H
#define _OPF_EXECUTIVE_H

#include <sstream>
#include <fstream>
#include <vector>

#include "gldcore/gridlabd.h"
#include "powerflow/solver_nr.h"

#include "powerflow/uot/uot_common.h"

#include "opf_controllable_load.h"

//Forward declare opf_controllable_load due to cyclic dependency
class opf_controllable_load;

class opf_executive : public gld_object {

public: // published variables
	// TODO add public typedefs
	// TODO declare published variables using GL_* macros
	int interval;
	char1024 file_name_load_data;
  char1024 file_name_ybus_data;
  char1024 file_name_node_data;
  char1024 file_name_controllable_load_information;
  char1024 file_name_controllable_load_setpoints;
	char1024 file_name_branch_data;
	char1024 file_name_spct_data;

    int execution_start_delay;

    typedef enum {
        NONE=0,
        INFO,
        WARNING,
        ERROR
    } output_type_enum;

    // We use this flag to avoid multiple notifications when the admittance changes more than once in a timestep.
    // It is set to false in every precommit and true after notifying a change in admittance_change_watchdog.
    output_type_enum admittance_change_output;

private: // unpublished variables
	// TODO add private typedefs
	// TODO add unpublished variables
	TIMESTAMP execution_start;
	TIMESTAMP next_execution;

	bool is_executing;
	bool is_first_run;

	int n_digit_output;
	std::vector<opf_controllable_load*> controlled_load_vec;

	TIMESTAMP time_controllable_load_values_next;

	int n_phase;

	std::ofstream * file_stream_load_data_ptr;
    std::ifstream * file_stream_controllable_load_setpoints_ptr;

    bool flag_admittance_change_acknowledged;



public: // required functions
	opf_executive(MODULE *module);
	int create(void);
	int init(OBJECT *parent);
	// TODO add optional class functions

public: // optional/user-defined functions
	// TODO add published class functions
	int precommit(TIMESTAMP t0);
	TIMESTAMP presync(TIMESTAMP t0);
	TIMESTAMP sync(TIMESTAMP t0);
	TIMESTAMP postsync(TIMESTAMP t0);
	TIMESTAMP commit(TIMESTAMP t1, TIMESTAMP t2);
	TIMESTAMP finalize();

	void RegisterControllableLoad(opf_controllable_load* controllable_load);

private: // internal functions
	// TODO add desired internal functions
	void print_load_data(unsigned int bus_count, BUSDATA *bus);
	void complex_array_to_csv_stream(std::ofstream & stream, complex *array,int n_array);
	void complex_vector_to_csv_stream(std::ofstream &stream, std::vector<complex> vec);

	void print_controllable_load_information();
	void print_phase_information(unsigned int bus_count, BUSDATA *bus);
  	void print_ybus(unsigned int bus_count, NR_SOLVER_STRUCT *powerflow_values, BUSDATA *bus);

  	void development_status_check(unsigned int bus_count, NR_SOLVER_STRUCT *powerflow_values, BUSDATA *bus);

	void initialize_parsing_controllable_load_values();

	void update_next_timestamp_controllable_load_values();

	void update_controllable_load_values();

    void add_node_loads_to_zip_load(zip_load_struct& zip_load, node * node_ptr);

	void phase_NR_to_stream(std::ofstream &stream, unsigned char phases);
	void phase_header_to_stream(std::ofstream &stream);

	void print_branch_information(unsigned int branch_count, BRANCHDATA *branch, BUSDATA *bus);

	void print_spct_information(unsigned int branch_count, BRANCHDATA *branch, BUSDATA *bus);

	void timestamp_to_stream(std::ofstream & stream);

	void timestamp_to_stream(std::ofstream * stream);

    void admittance_change_watchdog(bool admittance_change);

    void initialize_load_data();
	void finalize_load_data();

    void initialize_controllable_load_setpoints();
    void finalize_controllable_load_setpoints();


public: // required members
	static CLASS *oclass;
	static opf_executive *defaults;

};

struct sparse_triplet
{
  int row;
  int col;
  double val;
};

#endif // _OPF_EXECUTIVE_H
