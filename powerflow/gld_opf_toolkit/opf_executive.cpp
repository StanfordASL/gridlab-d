// $Id: opf_executive.cpp$
//	Copyright (C) 2017 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)
#include "opf_executive.h"

#include <cmath>

#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>

#include "powerflow/powerflow.h" // NR_busdata
#include "powerflow/link.h" // SubNode
#include "powerflow/node.h" // SubNode
#include "powerflow/solver_nr.h" // BUSDATA

#include "powerflow/uot/uot_common.h"

#include "opf_controllable_load.h"

//Based on solver_nr.h on defition of BUSDATA
#define PHASE_NR_S 128
#define PHASE_NR_HOUSE 64
#define PHASE_NR_D 8
#define PHASE_NR_A 4
#define PHASE_NR_B 2
#define PHASE_NR_C 1


EXPORT_CREATE(opf_executive);
EXPORT_INIT(opf_executive);
EXPORT_PRECOMMIT(opf_executive);
EXPORT_SYNC(opf_executive);
EXPORT_COMMIT(opf_executive);
EXPORT_FINALIZE(opf_executive);

// TODO add optional functions declarations
CLASS *opf_executive::oclass = NULL;
opf_executive *opf_executive::defaults = NULL;

// TODO add declaration of class globals

opf_executive::opf_executive(MODULE *module)
{
    if (oclass != NULL) {
        exception("cannot register class more than once");
    }

    oclass = gld_class::create(module, "opf_executive", sizeof(opf_executive),
                               PC_AUTOLOCK | PC_FORCE_NAME | PC_BOTTOMUP | PC_PRETOPDOWN);

    if (oclass == NULL) {
        exception("opf_executive class registration failed");

    }

    //Technology readiness level is 0 for now
    oclass->trl = TRL_UNKNOWN;

    if (gl_publish_variable(oclass,
                            PT_int32, "interval", PADDR(interval), PT_DESCRIPTION,
                            "seconds between runs of opf_executive",
                            PT_int32, "execution_start_delay", PADDR(execution_start_delay), PT_DESCRIPTION,
                            "seconds between start of simulation and first opf_executive run",
                            PT_char1024, "file_name_load_data", PADDR(file_name_load_data), PT_DESCRIPTION,
                            "file name for load data",
                            PT_char1024, "file_name_ybus_data", PADDR(file_name_ybus_data), PT_DESCRIPTION, "file name for Ybus",
                            PT_char1024, "file_name_node_data", PADDR(file_name_node_data),
                            PT_DESCRIPTION, "file name for node data",
                            PT_char1024, "file_name_controllable_load_information",
                            PADDR(file_name_controllable_load_information), PT_DESCRIPTION,
                            "file name for controllable load information",
                            PT_char1024, "file_name_controllable_load_setpoints",
                            PADDR(file_name_controllable_load_setpoints), PT_DESCRIPTION,
                            "file name for controllable load setpoints",
                            PT_char1024, "file_name_branch_data",
                            PADDR(file_name_branch_data), PT_DESCRIPTION,
                            "file name for branch data",
                            PT_char1024, "file_name_spct_data",
                            PADDR(file_name_spct_data), PT_DESCRIPTION,
                            "file name for single phase center tapped transformer data",
                            PT_enumeration, "admittance_change_output",PADDR(admittance_change_output),PT_DESCRIPTION,"Designation of output type for notifying changes in admittance.",
                            PT_KEYWORD, "NONE", (enumeration)NONE,
                            PT_KEYWORD, "INFO", (enumeration)INFO,
                            PT_KEYWORD, "WARNING", (enumeration)WARNING,
                            PT_KEYWORD, "ERROR", (enumeration)ERROR,
                            NULL) < 1)
        throw "unable to publish opf_executive variables";

    // this should not be done if virtual functions are used
    memset(defaults = this, 0, sizeof(*this));

    // TODO set defaults


}

int opf_executive::create(void) {
    if (solver_method != SM_NR) {
        gl_error("opf_executive only works for NR solver.");
        return FAILED;
    }

    memcpy(this, defaults, sizeof(*this));
    // TODO set defaults

    execution_start = -1;
    next_execution = -1;
    interval = -1;

    execution_start_delay = 1;

    is_executing = false;

    is_first_run = true;

    file_name_load_data.get_string()[0] = '\0';
    file_name_ybus_data.get_string()[0] = '\0';
    file_name_node_data.get_string()[0] = '\0';
    file_name_controllable_load_information.get_string()[0] = '\0';
    file_name_branch_data.get_string()[0] = '\0';
    file_name_spct_data.get_string()[0] = '\0';

    file_name_controllable_load_setpoints.get_string()[0] = '\0';

    time_controllable_load_values_next = TS_NEVER;

    n_digit_output = 15;

    n_phase = 3;

    admittance_change_output = WARNING;

    flag_admittance_change_acknowledged = false;

    file_stream_load_data_ptr = NULL;
    file_stream_controllable_load_setpoints_ptr = NULL;

    return SUCCESS; // return FAILED on create error
}

int opf_executive::init(OBJECT *parent) {
    // TODO initialize object
    if (interval < 0) {
        GL_THROW("Interval must be set to a positive value.");
    }

    if (execution_start_delay < 1) {
        GL_THROW("execution_start_delay must be set to a positive value.");
    }

    gl_output("Interval = %d", interval);

    this->initialize_load_data();

    this->initialize_controllable_load_setpoints();

    return SUCCESS; // return FAILED on create error
}

// TODO add implementations of optional class functions

int opf_executive::precommit(TIMESTAMP t0) {

    if (execution_start < 0) {
        // IF this is the first time precommit is called, register initial timestamp
        execution_start = t0 + execution_start_delay;

        // We execute for the first time after one interval passes to let everything else get ready
        next_execution = execution_start;

        // We set it here to true so that we do not get notified about the changes in the first timestep
        flag_admittance_change_acknowledged = true;
    } else {
        // Reset it to false
        flag_admittance_change_acknowledged = false;
    }

    if (t0 == next_execution) {
        is_executing = true;
        next_execution = t0 + interval;
    } else if (t0 > next_execution) {
        gl_warning("Time for next_execution = %s is in the past (t0 = %s)", timestamp_to_string(next_execution).c_str(), timestamp_to_string(t0).c_str());
    }

    if (is_executing) {
        std::string ts_str =  timestamp_to_string(t0);
        gl_output("t0 = %s and we are executing", ts_str.c_str());

        if (file_stream_load_data_ptr != NULL) {
            this->print_load_data(NR_bus_count, NR_busdata);
        }

        if (is_first_run) {
            this->development_status_check(NR_bus_count, &NR_powerflow, NR_busdata);

            if (file_name_ybus_data.get_string()[0] != '\0') {
                this->print_ybus(NR_bus_count, &NR_powerflow, NR_busdata);
            }
            if (file_name_node_data.get_string()[0] != '\0') {
                this->print_phase_information(NR_bus_count, NR_busdata);
            }
            if (file_name_controllable_load_information.get_string()[0] != '\0') {
                this->print_controllable_load_information();
            }
            if (file_name_branch_data.get_string()[0] != '\0') {
                this->print_branch_information(NR_branch_count,NR_branchdata,NR_busdata);
            }
            if (file_name_spct_data.get_string()[0] != '\0') {
                this->print_spct_information(NR_branch_count,NR_branchdata,NR_busdata);
            }

            is_first_run = false;
        }

    } else {
        gl_output("t0 = %s and we are sleeping", timestamp_to_string(t0).c_str());
    }
    return SUCCESS;
}


TIMESTAMP opf_executive::presync(TIMESTAMP t0) {
    gl_output("DEBUG: presync.");
    if (time_controllable_load_values_next < t0) {
        gl_warning(
                "time_controllable_load_values_next  = %d is in the past (t0 = %d). opf_controllable_loads will not be updated.",
                time_controllable_load_values_next, t0);
    } else if (t0 == time_controllable_load_values_next) {
        this->update_controllable_load_values();
    }

    this->admittance_change_watchdog(NR_admit_change);

    //  In principle, we could return time_controllable_load_values_next and update_controllable_load_values
    // at arbitrary timestamps given in file_name_controllable_load_setpoints. This is not done because we want
    // the timing to be controlled solely by interval.
    return TS_NEVER;
}


TIMESTAMP opf_executive::sync(TIMESTAMP t0) {
    TIMESTAMP t1 = next_execution;
    this->admittance_change_watchdog(NR_admit_change);
    // TODO add bottom-up sync process here
    // TODO set t1 to time of next event
    return t1; // return TS_INVALID on failure
}

TIMESTAMP opf_executive::postsync(TIMESTAMP t0) {
    this->admittance_change_watchdog(NR_admit_change);
    return TS_NEVER;
}

TIMESTAMP opf_executive::commit(TIMESTAMP t1, TIMESTAMP t2) {
    // TODO add commit process here
    is_executing = false;

    // TODO set t1 if soft or hard event is pending
    return TS_NEVER; // return TS_INVALID on failure
}

TIMESTAMP opf_executive::finalize()
{
    this->finalize_load_data();
    return SUCCESS;
}

void opf_executive::timestamp_to_stream(std::ofstream & stream)
{
    stream << "Timestamp: " << gl_globalclock << "\n";
}

void opf_executive::timestamp_to_stream(std::ofstream * stream)
{
    *stream << "Timestamp: " << gl_globalclock << "\n";
}


void opf_executive::initialize_load_data()
{
    if (file_name_load_data.get_string()[0] != '\0') {
        file_stream_load_data_ptr = new std::ofstream(file_name_load_data,std::ofstream::trunc);

        if (file_stream_load_data_ptr->is_open()) {
            file_stream_load_data_ptr->precision(n_digit_output);

            *file_stream_load_data_ptr << "Start of timeseries\n";

            *file_stream_load_data_ptr << "Name" << ',' <<
                                  "V_1" << ',' << "V_2" << ',' << "V_3" << ',' <<
                                  "S_y_1" << ',' << "S_y_2" << ',' << "S_y_3" << ',' << "S_d_1" << ',' << "S_d_2" << ','
                                  << "S_d_3"
                                  << ',' <<
                                  "Y_y_1" << ',' << "Y_y_2" << ',' << "Y_y_3" << ',' << "Y_d_1" << ',' << "Y_d_2" << ','
                                  << "Y_d_3"
                                  << ',' <<
                                  "I_y_1" << ',' << "I_y_2" << ',' << "I_y_3" << ',' << "I_d_1" << ',' << "I_d_2" << ','
                                  << "I_d_3\n\n";
        } else {
            GL_THROW("Could not open file to print_load_data");
        }
    }
}

void opf_executive::finalize_load_data()
{
    if (file_stream_load_data_ptr != NULL) {
        *file_stream_load_data_ptr << "End of timeseries";
        file_stream_load_data_ptr->close();
        delete file_stream_load_data_ptr;
    }
}

void opf_executive::initialize_controllable_load_setpoints()
{
    gl_output("initialize_controllable_load_setpoints");
    //If the user defined file_name_controllable_load_setpoints, then read timestamp for first update
    if (file_name_controllable_load_setpoints.get_string()[0] != '\0') {
        file_stream_controllable_load_setpoints_ptr = new std::ifstream(file_name_controllable_load_setpoints);

        if (file_stream_controllable_load_setpoints_ptr->is_open()) {

            std::string first_line;
            std::getline(*file_stream_controllable_load_setpoints_ptr, first_line);

            const std::string first_line_ref = "Start of timeseries";
            if (first_line.compare(first_line_ref) != 0) {
                GL_THROW("First line of controllable_load_setpoints is wrong.");
            }

            std::string header_line;
            std::getline(*file_stream_controllable_load_setpoints_ptr, header_line);

            const std::string header_line_ref = "Name,S_y_1_real,S_y_1_imag,S_y_2_real,S_y_2_imag,S_y_3_real,S_y_3_imag";
            if (header_line.compare(header_line_ref) != 0) {
                GL_THROW("Header line of controllable_load_setpoints is wrong.");
            }

            gl_output("DEBUG: header_line = %s",header_line.c_str());

            // Discard empty line
            std::string line;
            std::getline(*file_stream_controllable_load_setpoints_ptr, line);
            gl_output("DEBUG: line = %s",line.c_str());

//            std::getline(*file_stream_controllable_load_setpoints_ptr, line);
//            gl_output("DEBUG: line = %s",line.c_str());
//
//            std::getline(*file_stream_controllable_load_setpoints_ptr, line);
//            gl_output("DEBUG: line = %s",line.c_str());

            this->update_next_timestamp_controllable_load_values();
        } else {
            gl_warning(
                    "file_name_controllable_load_setpoints = %s was set but could not be opened.",file_name_controllable_load_setpoints);
        }
    }
}

void opf_executive::finalize_controllable_load_setpoints()
{
    if (file_stream_controllable_load_setpoints_ptr != NULL) {
        file_stream_controllable_load_setpoints_ptr->close();
        delete file_stream_controllable_load_setpoints_ptr;
    }
}


void opf_executive::print_load_data(unsigned int bus_count, BUSDATA *bus) {
    std::ofstream & file_stream_load_data = *file_stream_load_data_ptr;

   if (file_stream_load_data.is_open()) {
        this->timestamp_to_stream(file_stream_load_data_ptr);
        for (int i_bus = 0; i_bus < bus_count; ++i_bus) {
            file_stream_load_data << bus[i_bus].name << ',';

            this->complex_array_to_csv_stream(file_stream_load_data, bus[i_bus].V, n_phase);
            file_stream_load_data << ',';

            node *node_ptr = OBJECTDATA(bus[i_bus].obj, node);

            zip_load_struct zip_load;

            this->add_node_loads_to_zip_load(zip_load, node_ptr);

            // If the node is a parent, accumulate its children
            if (node_ptr->SubNode == PARENT || node_ptr->SubNode == DIFF_PARENT) {
                for (int i_child = 0; i_child < node_ptr->NR_number_child_nodes[0]; ++i_child) {
                    node *child_prt = node_ptr->NR_child_nodes[i_child];

                    this->add_node_loads_to_zip_load(zip_load, child_prt);
                }
            } else if (node_ptr->SubNode == DIFF_CHILD || node_ptr->SubNode == CHILD ||
                       node_ptr->SubNode == CHILD_NOINIT) {
                // This is a paranoid check and should not be true.
                GL_THROW("Unrecognized SubNode.");
            }

            this->complex_vector_to_csv_stream(file_stream_load_data, zip_load.S_y);
            file_stream_load_data << ',';
            this->complex_vector_to_csv_stream(file_stream_load_data, zip_load.S_d);
            file_stream_load_data << ',';
            this->complex_vector_to_csv_stream(file_stream_load_data, zip_load.Y_y);
            file_stream_load_data << ',';
            this->complex_vector_to_csv_stream(file_stream_load_data, zip_load.Y_d);
            file_stream_load_data << ',';
            this->complex_vector_to_csv_stream(file_stream_load_data, zip_load.I_y);
            file_stream_load_data << ',';
            this->complex_vector_to_csv_stream(file_stream_load_data, zip_load.I_d);
            file_stream_load_data << '\n';
        }
       file_stream_load_data << '\n';
    } //file_stream_load_data.is_open()
}

void opf_executive::add_node_loads_to_zip_load(zip_load_struct& zip_load, node * node_ptr)
{
    for (int i_phase = 0; i_phase < zip_load.n_phase; ++i_phase) {
        if (node_ptr->has_phase(PHASE_D)) {
            zip_load.S_d[i_phase] += node_ptr->power[i_phase];
            zip_load.Y_d[i_phase] += node_ptr->shunt[i_phase];
            zip_load.I_d[i_phase] += node_ptr->current[i_phase];
        } else {
            zip_load.S_y[i_phase] += node_ptr->power[i_phase];
            zip_load.Y_y[i_phase] += node_ptr->shunt[i_phase];
            zip_load.I_y[i_phase] += node_ptr->current[i_phase];
        }

        zip_load.S_d[i_phase] += node_ptr->power_dy[i_phase];
        zip_load.S_y[i_phase] += node_ptr->power_dy[i_phase + zip_load.n_phase];

        zip_load.Y_d[i_phase] += node_ptr->shunt_dy[i_phase];
        zip_load.Y_y[i_phase] += node_ptr->shunt_dy[i_phase + zip_load.n_phase];

        zip_load.I_d[i_phase] += node_ptr->current_dy[i_phase];
        zip_load.I_y[i_phase] += node_ptr->current_dy[i_phase + zip_load.n_phase];

        // For house nodes, accumulate nom_res_curr too
        //From compute_load_values in solver_nr, we see that house currents need to be rotated
        if (node_ptr->house_present) {
            complex rotation_factor;

            if (i_phase == 2) {
                complex voltage_12 = node_ptr->voltage[0] + node_ptr->voltage[1];
                rotation_factor.SetPolar(1.0,voltage_12.Arg());
            } else {
                rotation_factor.SetPolar(1.0,node_ptr->voltage[i_phase].Arg());
            }

            const complex house_current_phase = node_ptr->nom_res_curr[i_phase]/(~rotation_factor);

            zip_load.I_y[i_phase] += node_ptr->nom_res_curr[i_phase];
        }
    }

    if (node_ptr->has_phase(PHASE_S)) {
        //For split phase nodes, add current12 to I[2]
        zip_load.I_y[2] += node_ptr->current12;
    }
}

void opf_executive::complex_vector_to_csv_stream(std::ofstream &stream, std::vector<complex> vec)
{
    this->complex_array_to_csv_stream(stream, &vec[0], vec.size());
}

void opf_executive::complex_array_to_csv_stream(std::ofstream &stream, complex *array, int n_array)
{
    for (int i_array = 0; i_array < n_array; ++i_array) {
        stream << array[i_array];

        if (i_array < n_array - 1) {
            stream << ',';
        }
    }
}

void opf_executive::print_phase_information(unsigned int bus_count, BUSDATA *bus) {
    std::ofstream file_stream;

    file_stream.open(file_name_node_data);

    if (file_stream.is_open()) {
        file_stream << "Name,Volt_base,";
        this->phase_header_to_stream(file_stream);
        file_stream << '\n';

        for (int i_bus = 0; i_bus < bus_count; ++i_bus) {

            file_stream << bus[i_bus].name << ',';

            file_stream << bus[i_bus].volt_base << ',';

            this->phase_NR_to_stream(file_stream,bus[i_bus].phases);
            file_stream << '\n';
        }

        file_stream.close();

    } //file_stream.is_open()
    else {
        GL_THROW("Could not open file to print_phase_information.");
    }
}

void opf_executive::print_branch_information(unsigned int branch_count, BRANCHDATA *branch, BUSDATA *bus)
{
    std::ofstream file_stream;
    file_stream.open(file_name_branch_data);

    if (file_stream.is_open()) {
        this->timestamp_to_stream(file_stream);

        file_stream << "Name,From,To,";
        this->phase_header_to_stream(file_stream);
        file_stream << '\n';

        for (int i_branch = 0; i_branch < branch_count; ++i_branch) {

            file_stream << branch[i_branch].name << ',';

            const int from_bus_index = branch[i_branch].from;
            const int to_bus_index = branch[i_branch].to;

            file_stream << bus[from_bus_index].name << ',';
            file_stream << bus[to_bus_index].name << ',';

            this->phase_NR_to_stream(file_stream, branch[i_branch].phases);
            file_stream << '\n';
        }

        file_stream.close();

    } //file_stream.is_open()
    else {
        if (file_name_branch_data.get_string()[0] != '\0'){
            GL_THROW("Could not open file to print_branch_information.");
        }
    }
}

void opf_executive::print_spct_information(unsigned int branch_count, BRANCHDATA *branch, BUSDATA *bus)
{
    std::ofstream file_stream;
    file_stream.open(file_name_spct_data);

    file_stream.precision(n_digit_output);

    if (file_stream.is_open()) {
        this->timestamp_to_stream(file_stream);

        file_stream << "Name,From,To,S_in_A,S_in_B,S_in_C\n";

        for (int i_branch = 0; i_branch < branch_count; ++i_branch) {

            link_object *link_ptr = OBJECTDATA(branch[i_branch].obj, link_object);

            // Check that branch is SCPT.
            if (link_ptr->SpecialLnk == SPLITPHASE) {
                file_stream << branch[i_branch].name << ',';

                const int from_bus_index = branch[i_branch].from;
                const int to_bus_index = branch[i_branch].to;

                file_stream << bus[from_bus_index].name << ',';
                file_stream << bus[to_bus_index].name << ',';

                this->complex_array_to_csv_stream(file_stream, link_ptr->indiv_power_in, n_phase);
                file_stream << '\n';
            }
        }

        file_stream.close();
    } //file_stream.is_open()
    else {
        if (file_name_spct_data.get_string()[0] != '\0'){
            GL_THROW("Could not open file to print_spct_information.");
        }
    }
}

void opf_executive::phase_NR_to_stream(std::ofstream &stream, unsigned char phases)
{
    const bool phase_a = (phases & PHASE_NR_A) == PHASE_NR_A;
    const bool phase_b = (phases & PHASE_NR_B) == PHASE_NR_B;
    const bool phase_c = (phases & PHASE_NR_C) == PHASE_NR_C;
    const bool phase_d = (phases & PHASE_NR_D) == PHASE_NR_D;
    const bool phase_s = (phases & PHASE_NR_S) == PHASE_NR_S;

    stream << phase_a << ',' << phase_b << ','
           << phase_c << ',' << phase_d << ',' << phase_s;
}

void opf_executive::phase_header_to_stream(std::ofstream &stream)
{
    stream << "PhaseA,PhaseB,PhaseC,PhaseD,PhaseS";
}

void opf_executive::print_ybus(unsigned int bus_count, NR_SOLVER_STRUCT *powerflow_values, BUSDATA *bus) {
    std::ofstream file_stream;

    file_stream.precision(n_digit_output);

    file_stream.open(file_name_ybus_data);

    if (file_stream.is_open()) {

        file_stream << "Note: All indices are zero-referenced.\n\n";

        //Dump entry information
        file_stream << "Matrix Index information for this call - start,stop,name\n";

        //Based on code from solver_nr lines around 3090
        for (int i = 0; i < bus_count; ++i) {
            const int start = 2 * bus[i].Matrix_Loc;
            const int end = start + 2 * powerflow_values->BA_diag[i].size - 1;

            file_stream << start << ',' << end << ',' << bus[i].name << '\n';
        }

        file_stream << '\n';

        //Based on code from solver_nr lines around 3050
        //Last element is for the diagonal entries
        int size_ybus = powerflow_values->size_offdiag_PQ * 2 + powerflow_values->size_diag_fixed * 2 + 6 * bus_count;

        std::vector<sparse_triplet> triplet_vec;
        triplet_vec.reserve(size_ybus);

        //Integrate off diagonal components
        for (int i = 0; i < powerflow_values->size_offdiag_PQ * 2; ++i) {
            const int row = powerflow_values->Y_offdiag_PQ[i].row_ind;
            const int col = powerflow_values->Y_offdiag_PQ[i].col_ind;
            const double value = powerflow_values->Y_offdiag_PQ[i].Y_value;
            triplet_vec.push_back(sparse_triplet{row, col, value});
        }

        //Integrate fixed portions of diagonal components
        for (int i = 0; i < powerflow_values->size_diag_fixed * 2; ++i) {
            const int row = powerflow_values->Y_diag_fixed[i].row_ind;
            const int col = powerflow_values->Y_diag_fixed[i].col_ind;
            const double value = powerflow_values->Y_diag_fixed[i].Y_value;
            triplet_vec.push_back(sparse_triplet{row, col, value});
        }

        //Note from lines 2137 and 2150 in solver_nr.cpp that Y_diag_fixed does not include entries in the actual diagonal.
        // Only in the diagonal blocks. The actual diagonal is added in Y_diag_update Hence, we need to add them too.

        //Based on lines 2952 to 3005
        for (int i = 0; i < bus_count; i++)    //Parse through bus list
        {
            for (int j = 0; j < powerflow_values->BA_diag[i].size; j++) {
                const int ind_1 = 2 * bus[i].Matrix_Loc + j;
                const int ind_2 = ind_1 + powerflow_values->BA_diag[i].size;

                const double G = powerflow_values->BA_diag[i].Y[j][j].Re();
                const double B = powerflow_values->BA_diag[i].Y[j][j].Im();

                triplet_vec.push_back(sparse_triplet{ind_2, ind_1, G});
                triplet_vec.push_back(sparse_triplet{ind_1, ind_2, G});

                triplet_vec.push_back(sparse_triplet{ind_1, ind_1, B});
                triplet_vec.push_back(sparse_triplet{ind_2, ind_2, -B});
            }
        }//End bus parse list



        file_stream << "Timestamp: " << gl_globalclock << "\n";
        file_stream << "Matrix Information - non-zero element count = " << triplet_vec.size() << "\n";
        file_stream << "Matrix Information - row, column, value\n";

        for (std::vector<sparse_triplet>::iterator data_it = triplet_vec.begin();
             data_it != triplet_vec.end(); ++data_it) {
            file_stream << data_it->row << ',' << data_it->col << ',' << data_it->val << '\n';
        }

        file_stream.close();
    } // file_stream.is_open()
    else {
        GL_THROW("Could not open file to print_ybus.");
    }
}

// Goes through NR_powerflow and NR_busdata to check that the model does not include untested features
void opf_executive::development_status_check(unsigned int bus_count, NR_SOLVER_STRUCT *powerflow_values, BUSDATA *bus) {
    for (int i_bus = 0; i_bus < bus_count; ++i_bus) {
       node *node_ptr = OBJECTDATA(bus[i_bus].obj, node);
        if (node_ptr->house_present) {
            gl_warning("Model includes a node with a house. Functionality has not been tested.");
        }
    }
}

void opf_executive::RegisterControllableLoad(opf_controllable_load *controllable_load) {
    controlled_load_vec.push_back(controllable_load);
}

void opf_executive::print_controllable_load_information() {
    std::ofstream file_stream;

    file_stream.open(file_name_controllable_load_information);

    if (file_stream.is_open()) {
        file_stream << "Name,Attachment_point,PhaseA,PhaseB,PhaseC\n";
        //controllable_load->has_phase(PHASE_A)

        for (int i = 0; i < controlled_load_vec.size(); ++i) {
            opf_controllable_load *controlled_load = controlled_load_vec[i];
            OBJECT *controlled_load_obj = OBJECTHDR(controlled_load);

            file_stream << controlled_load_obj->name << ',';

            //If we are a child, our parent is the attachemnt point
            if (controlled_load->SubNode == CHILD || controlled_load->SubNode == DIFF_CHILD ||
                controlled_load->SubNode == CHILD_NOINIT) {
                file_stream << controlled_load->SubNodeParent->name;
            } else {
                file_stream << controlled_load_obj->name;
            }

            //Here we work with a node object (opf_controllable_load is a subclass thereof). Hence, we use PHASE_X
            const bool phase_a = controlled_load->has_phase(PHASE_A);
            const bool phase_b = controlled_load->has_phase(PHASE_B);
            const bool phase_c = controlled_load->has_phase(PHASE_C);

            file_stream << ',' << phase_a << ',' << phase_b << ',' << phase_c << '\n';

        }

        file_stream.close();
    } //file_stream.is_open()
    else {
        GL_THROW("Could not open file to print_controllable_load_information.");
    }
}

void opf_executive::update_next_timestamp_controllable_load_values() {
    gl_output("DEBUG: update_next_timestamp_controllable_load_values");
    TIMESTAMP time_controllable_load_values = TS_INVALID;

    std::ifstream & file_stream = *file_stream_controllable_load_setpoints_ptr;

    if (file_stream.is_open()) {
        std::string line;
        std::getline(file_stream,line);
        gl_output("DEBUG: update_next_timestamp_controllable_load_values. line = %s",line.c_str());

        std::istringstream line_stream(line);

        std::string timestamp_text;
        line_stream >> timestamp_text;

        //timestamp_text.compare returns 0 if the strings match
        if (timestamp_text.compare("Timestamp:") == 0) {
            line_stream >> time_controllable_load_values;

        } else if (line.compare("End of timeseries") == 0) {
            gl_output("Done updating controllable loads.");
            time_controllable_load_values = TS_NEVER;

        } else {
            GL_THROW("Format of file_name_controllable_load_information is wrong at %s.",timestamp_text.c_str());
        }
    } //file_stream.is_open()
    else {
        GL_THROW("Error reading file_stream_controllable_load_setpoints.");
    }
    //If we could not open the file, probably it does not exist so no need to update

    time_controllable_load_values_next = time_controllable_load_values;
}

void opf_executive::update_controllable_load_values() {
    gl_output("DEBUG: update_controllable_load_values.");
    std::ifstream & file_stream = *file_stream_controllable_load_setpoints_ptr;

    if (file_stream.is_open()) {
        std::string line;

        // Block for a timestamp ends with an empty line
        while (std::getline(file_stream, line) && !line.empty()) {
            std::istringstream line_stream(line);

            std::string controllable_load_name;
            std::vector<double> S_y(6, 0);

            std::getline(line_stream, controllable_load_name, ',');

            for (int i_S_y = 0; i_S_y < S_y.size(); ++i_S_y) {
                std::string number_str;
                std::getline(line_stream, number_str, ',');

                std::stringstream number_stream(number_str);

                number_stream >> S_y[i_S_y];

                gl_output("i_S_y = %d, S_y[i_S_y] = %f", i_S_y, S_y[i_S_y]);

                //Check that line_stream is still good before reading the last element
                if (i_S_y != S_y.size() - 1 && !line_stream.good()) {
                    GL_THROW(
                            "Could not read values for opf_controllable load %s. Format of file_name_controllable_load_information is probably wrong.",
                            controllable_load_name.c_str());
                }
            }

            if (!line_stream.eof()) {
                GL_THROW(
                        "Line for opf_controllable load %s did not end as expected. Format of file_name_controllable_load_information is probably wrong.",
                        controllable_load_name.c_str());
            }

            OBJECT *controllable_load_obj = gl_get_object(&controllable_load_name[0]);

            if (controllable_load_obj == NULL) {
                GL_THROW("Core could not find opf_controllable_load %s.", controllable_load_name.c_str());
            }

            opf_controllable_load *controllable_load = OBJECTDATA(controllable_load_obj, opf_controllable_load);

            if (!controllable_load->has_phase(PHASE_A) && (S_y[0] != 0 || S_y[1] != 0)) {
                gl_warning(
                        "Nonzero load in phase A for opf_controllable_load %s, but load does not have phase A. Value ignored.",
                        controllable_load_name.c_str());
                S_y[0] = 0;
                S_y[1] = 0;
            }
            if (!controllable_load->has_phase(PHASE_B) && (S_y[2] != 0 || S_y[3] != 0)) {
                gl_warning(
                        "Nonzero load in phase B for opf_controllable_load %s, but load does not have phase A. Value ignored.",
                        controllable_load_name.c_str());
                S_y[2] = 0;
                S_y[3] = 0;
            }
            if (!controllable_load->has_phase(PHASE_C) && (S_y[4] != 0 || S_y[5] != 0)) {
                gl_warning(
                        "Nonzero load in phase C for opf_controllable_load %s, but load does not have phase A. Value ignored.",
                        controllable_load_name.c_str());
                S_y[4] = 0;
                S_y[5] = 0;
            }

            for (int i_phase = 0; i_phase < n_phase; ++i_phase) {
                controllable_load->constant_power[i_phase] = complex(S_y[2 * i_phase], S_y[2 * i_phase + 1]);
            }

            gl_output("Finished updating opf_controllable_load %s.", controllable_load_name.c_str());
        }

        this->update_next_timestamp_controllable_load_values();

    } //file_stream.is_open()
    else {
        GL_THROW("Error reading file in update_controllable_load_values.");
    }
}


void opf_executive::admittance_change_watchdog(bool admittance_change)
{
     if(!flag_admittance_change_acknowledged && admittance_change) {
        std::stringstream message_stream("Admittance change detected.");
        std::string message = message_stream.str();

        switch (admittance_change_output) {
            case ERROR:
                GL_THROW(&message[0]);
                break;
            case WARNING:
                gl_warning(message.c_str());
                break;
            case INFO:
                gl_output(message.c_str());
                break;
        }
        flag_admittance_change_acknowledged = true;
    }
}












