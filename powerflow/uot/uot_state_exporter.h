// $Id: uot_state_exporter.h$
//	Copyright (C) 2018 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)

#ifndef _UOT_STATE_EXPORTER_H
#define _UOT_STATE_EXPORTER_H

#include <fstream>

#include "gldcore/gridlabd.h"
#include "powerflow/node.h"
#include "powerflow/solver_nr.h"
#include "powerflow/powerflow.h"
#include "powerflow/uot/uot_common.h"


class uot_state_exporter : public gld_object {
// This class implements functionality to export the loads, voltages and currents
// in the network as the simulation progresses.

public: // published variables
    int interval;
    char1024 file_name_current_data;
    char1024 file_name_load_data;
    char1024 file_name_swing_load_data;
    char1024 file_name_voltage_data;

private: // unpublished variables
    bool is_executing;
    int n_phase;
    TIMESTAMP next_execution;
    std::ofstream * file_stream_current_data_ptr;
    std::ofstream * file_stream_load_data_ptr;
    std::ofstream * file_stream_swing_load_data_ptr;
    std::ofstream * file_stream_voltage_data_ptr;

public: // required functions
    uot_state_exporter(MODULE *module);
    int create(void);
    int init(OBJECT *parent);

public: // optional/user-defined functions
    int precommit(TIMESTAMP t0);
    TIMESTAMP commit(TIMESTAMP t1, TIMESTAMP t2);
    TIMESTAMP finalize();

private: // internal functions
    void add_node_loads_to_zip_load(uot_zip_load_struct& zip_load, node * node_ptr);
    void complex_array_to_csv_stream(std::ofstream &stream, complex *array, int n_array);
    void complex_vector_to_csv_stream(std::ofstream &stream, std::vector<complex> vec);
    void finalize_data_file(std::ofstream*  file_stream_ptr);
    std::ofstream* intialize_data_file(char1024 & file_name, std::string & header);
    void print_load_data(TIMESTAMP ts, unsigned int bus_count, BUSDATA *bus);
    void print_voltage_data(TIMESTAMP ts, unsigned int bus_count, BUSDATA *bus);
    void print_current_data(TIMESTAMP ts, unsigned int branch_count, BRANCHDATA *branch, BUSDATA *bus);
    void print_swing_load_data(TIMESTAMP ts, int i_swing_bus, BRANCHDATA *branch, BUSDATA *bus);
    void timestamp_to_stream(TIMESTAMP ts,std::ofstream & stream);

public: // required members
    static CLASS *oclass;
    static uot_state_exporter *defaults;
};


#endif //_UOT_STATE_EXPORTER_H
