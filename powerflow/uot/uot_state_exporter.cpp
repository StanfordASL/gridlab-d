// $Id: uot_state_exporter.cpp$
//	Copyright (C) 2018 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)

#include "uot_state_exporter.h"

#include <limits>
#include <fstream>

#include "powerflow/link.h"
#include "powerflow/node.h"
#include "powerflow/powerflow.h"
#include "powerflow/solver_nr.h"

#include "powerflow/uot/uot_common.h"


EXPORT_CREATE(uot_state_exporter);
EXPORT_INIT(uot_state_exporter);

EXPORT_PRECOMMIT(uot_state_exporter);
EXPORT_COMMIT(uot_state_exporter);
EXPORT_FINALIZE(uot_state_exporter);

CLASS *uot_state_exporter::oclass = NULL;
uot_state_exporter *uot_state_exporter::defaults = NULL;

uot_state_exporter::uot_state_exporter(MODULE *module)
{
    if (oclass != NULL) {
        exception("cannot register class more than once");
    }

    oclass = gld_class::create(module, "uot_state_exporter", sizeof(uot_state_exporter),
                               PC_AUTOLOCK | PC_OBSERVER);

    if (oclass == NULL) {
        exception("uot_state_exporter class registration failed");
    }

    // Tests verified that output from this class can be used to replicate Gridlab-D power flow solutions and IEEE
    // test feeder specifications.
    oclass->trl = TRL_DEMONSTRATED;

    if (gl_publish_variable(oclass,
                            PT_int32, "interval", PADDR(interval), PT_DESCRIPTION,
                            "seconds between runs of uot_state_exporter",
                            PT_char1024, "file_name_load_data", PADDR(file_name_load_data), PT_DESCRIPTION,
                            "file name for load data",
                            PT_char1024, "file_name_voltage_data", PADDR(file_name_voltage_data), PT_DESCRIPTION,
                            "file name for voltage data",
                            PT_char1024, "file_name_current_data", PADDR(file_name_current_data), PT_DESCRIPTION,
                            "file name for current data",
                            PT_char1024, "file_name_swing_load_data", PADDR(file_name_swing_load_data), PT_DESCRIPTION,
                            "file name for swing load data",
                            NULL) < 1)
        throw "unable to publish uot_state_exporter variables";

    // this should not be done if virtual functions are used
    memset(defaults = this, 0, sizeof(*this));
}

int uot_state_exporter::create(void)
{
    if (solver_method != SM_NR) {
        gl_error("uot_state_exporter only works for NR solver.");
        return FAILED;
    }

    memcpy(this, defaults, sizeof(*this));

    // published variables defaults
    interval = -1;
    file_name_load_data.get_string()[0] = '\0';
    file_name_voltage_data.get_string()[0] = '\0';
    file_name_current_data.get_string()[0] = '\0';
    file_name_swing_load_data.get_string()[0] = '\0';

    // private variables defaults
    file_stream_load_data_ptr = NULL;
    file_stream_voltage_data_ptr = NULL;
    file_stream_current_data_ptr = NULL;
    file_stream_swing_load_data_ptr = NULL;

    is_executing = false;
    next_execution = -1;

    n_phase = 3;

    return SUCCESS; // return FAILED on create error
}

int uot_state_exporter::init(OBJECT *parent)
{
    if (interval < 0) {
        GL_THROW("Interval must be set to a positive value.");
    }

    gl_output("Interval = %d", interval);

    std::string load_data_header = "Name,S_y_1,S_y_2,S_y_3,S_d_1,S_d_2,S_d_3,Y_y_1,Y_y_2,Y_y_3,Y_d_1,Y_d_2,Y_d_3,I_y_1,I_y_2,I_y_3,I_d_1,I_d_2,I_d_3";
    std::string voltage_data_header = "Name,V_1,V_2,V_3";
    std::string current_data_header = "Name,From,To,current_in_1,current_in_2,current_in_3,current_out_1,current_out_2,current_out_3";
    std::string swing_load_data_header = "Name,S_swing_1,S_swing_2,S_swing_3";

    file_stream_load_data_ptr = this->intialize_data_file(file_name_load_data,load_data_header);
    file_stream_voltage_data_ptr = this->intialize_data_file(file_name_voltage_data,voltage_data_header);
    file_stream_current_data_ptr = this->intialize_data_file(file_name_current_data,current_data_header);
    file_stream_swing_load_data_ptr = this->intialize_data_file(file_name_swing_load_data,swing_load_data_header);

    return SUCCESS; // return FAILED on create error
}

int uot_state_exporter::precommit(TIMESTAMP t0)
{
    if (next_execution < 0) {
        // If this is the first time precommit is called, register initial timestamp
        next_execution = t0;
    }

    return SUCCESS;
}

TIMESTAMP uot_state_exporter::commit(TIMESTAMP t1, TIMESTAMP t2)
{
    if (t1 == next_execution) {
        gl_output("t1 = %s and we are executing", timestamp_to_string(t1).c_str());
        if (file_stream_load_data_ptr != NULL) {
            this->print_load_data(t1, NR_bus_count, NR_busdata);
        }

        if (file_stream_voltage_data_ptr != NULL) {
            this->print_voltage_data(t1, NR_bus_count, NR_busdata);
        }

        if (file_stream_current_data_ptr != NULL) {
            this->print_current_data(t1, NR_branch_count,NR_branchdata,NR_busdata);
        }

        if (file_stream_swing_load_data_ptr != NULL) {
            this->print_swing_load_data(t1, NR_swing_bus_reference, NR_branchdata,NR_busdata);
        }

        next_execution += interval;

    } else if (t1 > next_execution) {
        gl_warning("Time for next_execution = %s is in the past (t1 = %s)", timestamp_to_string(next_execution).c_str(), timestamp_to_string(t1).c_str());
        next_execution = TS_INVALID;

    } else {
        gl_output("t1 = %s and we are sleeping", timestamp_to_string(t1).c_str());
    }

    return next_execution; // return TS_INVALID on failure
}

TIMESTAMP uot_state_exporter::finalize()
{
    this->finalize_data_file(file_stream_load_data_ptr);
    this->finalize_data_file(file_stream_voltage_data_ptr);
    this->finalize_data_file(file_stream_current_data_ptr);
    this->finalize_data_file(file_stream_swing_load_data_ptr);

    return SUCCESS;
}

std::ofstream* uot_state_exporter::intialize_data_file(char1024 & file_name, std::string & header)
{
    // Initializes a file using the timeseries file format:
    // Start of timeseries
    // Header

    std::ofstream* file_stream_ptr = NULL;

    if (file_name.get_string()[0] != '\0') {
        file_stream_ptr = new std::ofstream(file_name,std::ofstream::trunc);

        if (file_stream_ptr->is_open()) {
            // Print enough digits of floating point numbers so that we do not
            // lose information
            file_stream_ptr->precision(std::numeric_limits<double>::digits10 + 1);

            *file_stream_ptr << "Start of timeseries\n";
            *file_stream_ptr << header << "\n\n";

        } else {
            GL_THROW("Could not open file %s",file_name);
        }
    }

    return file_stream_ptr;
}

void uot_state_exporter::finalize_data_file(std::ofstream* file_stream_ptr)
{
    // Finalizes a file using the timeseries file format:
    // End of timeseries

    if (file_stream_ptr != NULL) {
        *file_stream_ptr << "End of timeseries";
        file_stream_ptr->close();
        delete file_stream_ptr;
    }
}

void uot_state_exporter::timestamp_to_stream(TIMESTAMP ts,std::ofstream & stream)
{
    // Prints timestamp to stream according to timeseries file format (using POSIX time)
    // Timestamp: 11111111
    stream << "Timestamp: " << ts << "\n";
}

void uot_state_exporter::print_load_data(TIMESTAMP ts, unsigned int bus_count, BUSDATA *bus)
{
    // Prints a block in time series format for load data according to load_data_header

    std::ofstream & file_stream_load_data = *file_stream_load_data_ptr;

    if (file_stream_load_data.is_open()) {
        this->timestamp_to_stream(ts, file_stream_load_data);
        for (int i_bus = 0; i_bus < bus_count; ++i_bus) {
            file_stream_load_data << bus[i_bus].name << ',';

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

void uot_state_exporter::add_node_loads_to_zip_load(zip_load_struct& zip_load, node* node_ptr)
{
    // Accumulates the loads connected to a node in a zip_load_struct

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
        // From compute_load_values in solver_nr, we see that house currents need to be rotated
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

void uot_state_exporter::print_voltage_data(TIMESTAMP ts, unsigned int bus_count, BUSDATA *bus)
{
    // Prints a block in time series format for voltage data according to voltage_data_header

    std::ofstream & file_stream_voltage_data = *file_stream_voltage_data_ptr;

    const int n_phase = 3;

    if (file_stream_voltage_data.is_open()) {
        this->timestamp_to_stream(ts, file_stream_voltage_data);
        for (int i_bus = 0; i_bus < bus_count; ++i_bus) {
            file_stream_voltage_data << bus[i_bus].name << ',';

            this->complex_array_to_csv_stream(file_stream_voltage_data, bus[i_bus].V, n_phase);
            file_stream_voltage_data << '\n';
        }
        file_stream_voltage_data << '\n';
    } //file_stream_voltage_data.is_open()
}

void uot_state_exporter::print_current_data(TIMESTAMP ts, unsigned int branch_count, BRANCHDATA *branch, BUSDATA *bus)
{
    // Prints a block in time series format for current data according to current_data_header

    std::ofstream & file_stream_current_data = *file_stream_current_data_ptr;

    if (file_stream_current_data.is_open()) {
        this->timestamp_to_stream(ts, file_stream_current_data);

        for (int i_branch = 0; i_branch < branch_count; ++i_branch) {
            file_stream_current_data << branch[i_branch].name << ',';

            const int from_bus_index = branch[i_branch].from;
            const int to_bus_index = branch[i_branch].to;

            file_stream_current_data << bus[from_bus_index].name << ',';
            file_stream_current_data << bus[to_bus_index].name << ',';

            link_object* link_ptr = OBJECTDATA(branch[i_branch].obj, link_object);

            this->complex_array_to_csv_stream(file_stream_current_data, link_ptr->read_I_in, n_phase);
            file_stream_current_data << ',';
            this->complex_array_to_csv_stream(file_stream_current_data, link_ptr->read_I_out, n_phase);

            file_stream_current_data << '\n';
        }
        file_stream_current_data << '\n';
    } //file_stream_current_data.is_open()
}

void uot_state_exporter::print_swing_load_data(TIMESTAMP ts, int i_swing_bus, BRANCHDATA *branch, BUSDATA *bus)
{
    // Prints a block in time series format for swing load data according to swing_load_data_header

    std::ofstream & file_stream_swing_load_data = *file_stream_swing_load_data_ptr;

    if (file_stream_swing_load_data.is_open()) {
        this->timestamp_to_stream(ts, file_stream_swing_load_data);

        BUSDATA swing_bus = bus[i_swing_bus];

        int *swing_link_table = swing_bus.Link_Table;
        const int n_swing_link_table = swing_bus.Link_Table_Size;

        std::vector<complex> swing_current(n_phase, 0);

        for (int i_swing_link_table = 0; i_swing_link_table < n_swing_link_table; ++i_swing_link_table) {

            const int i_branch = swing_link_table[i_swing_link_table];
            BRANCHDATA this_branch = branch[i_branch];

            link_object *this_link_ptr = OBJECTDATA(this_branch.obj, link_object);

            const int from = this_branch.from;
            const int to = this_branch.to;

            complex *current_array;

            if (i_swing_bus == from) {
                current_array = this_link_ptr->read_I_in;

            } else if (i_swing_bus == to) {
                current_array = this_link_ptr->read_I_out;

            } else {
                GL_THROW("Branch %s is not connected to swing bus", this_branch.name);
                // We expect that all these branches are connected to the swing bus.
            }

            for (int i_phase = 0; i_phase < n_phase; ++i_phase) {
                swing_current[i_phase] += current_array[i_phase];
            }
        }

        file_stream_swing_load_data << swing_bus.name << ',';

        std::vector<complex> swing_power(n_phase, 0);

        for (int i_phase = 0; i_phase < n_phase; ++i_phase) {
            complex voltage = swing_bus.V[i_phase];
            swing_power[i_phase] = voltage*(~swing_current[i_phase]);
        }

        this->complex_vector_to_csv_stream(file_stream_swing_load_data,swing_power);

        file_stream_swing_load_data << "\n\n";
    } // file_stream_swing_load_data.is_open()
}

void uot_state_exporter::complex_array_to_csv_stream(std::ofstream &stream, complex *array, int n_array)
{
    // Outputs a complex array to a stream

    for (int i_array = 0; i_array < n_array; ++i_array) {
        stream << array[i_array];

        if (i_array < n_array - 1) {
            stream << ',';
        }
    }
}

void uot_state_exporter::complex_vector_to_csv_stream(std::ofstream &stream, std::vector<complex> vec)
{
    // Outputs a complex vector to a stream
    this->complex_array_to_csv_stream(stream, &vec[0], vec.size());
}