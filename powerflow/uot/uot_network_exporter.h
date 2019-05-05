// $Id: uot_network_exporter.h$
//	Copyright (C) 2018 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)

#ifndef _UOT_NETWORK_EXPORTER_H
#define _UOT_NETWORK_EXPORTER_H

#include <sstream>
#include <fstream>

#include "gldcore/gridlabd.h"
#include "powerflow/solver_nr.h"


class uot_network_exporter : public gld_object {
// This class implements functionality to export the power network
// used in a GridLAB-D model to the outside.

public: // published variables
    typedef enum {
        NONE=0,
        INFO,
        WARNING,
        ERROR
    } output_type_enum;

    char1024 file_name_node_data;
    char1024 file_name_branch_data;

    output_type_enum admittance_change_output;

private: // unpublished variables
    // We use this flag to avoid multiple notifications when the admittance changes more than once in a timestep.
    // It is set to false in every precommit and true after notifying a change in admittance_change_watchdog.
    bool flag_admittance_change_acknowledged;

    // Currently, we only allow for exporting the network after the first time step since
    // UOT assumes a time-invariant network.
    // In future, this can be extended to export the network again when it changes.
    bool is_first_run;

public: // required functions
    uot_network_exporter(MODULE *module);
    int create(void);
    int init(OBJECT *parent);

public: // optional/user-defined functions
    TIMESTAMP presync(TIMESTAMP t0);
    TIMESTAMP sync(TIMESTAMP t0);
    TIMESTAMP postsync(TIMESTAMP t0);
    TIMESTAMP commit(TIMESTAMP t1, TIMESTAMP t2);

private: // internal functions
    void admittance_change_watchdog(bool admittance_change);
    void development_status_check(unsigned int bus_count, NR_SOLVER_STRUCT *powerflow_values, BUSDATA *bus);
    void link_type_to_stream(std::ofstream &stream, unsigned char branch_lnk_type, unsigned char bus_to_phases);
    void matrix_to_stream(std::ofstream &stream, complex * X, int M, int N);
    void phase_header_to_stream(std::ofstream &stream);
    void phase_NR_to_stream(std::ofstream &stream, unsigned char phases);
    void print_branch_information(unsigned int branch_count, BRANCHDATA *branch, BUSDATA *bus);
    void print_node_information(unsigned int bus_count, BUSDATA *bus);

public: // required members
    static CLASS *oclass;
    static uot_network_exporter *defaults;
};


#endif //_UOT_NETWORK_EXPORTER_H
