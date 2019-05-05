// $Id: uot_network_exporter.cpp$
//	Copyright (C) 2018 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)
#include "uot_network_exporter.h"

#include <limits>

#include "powerflow/uot/uot_common.h"

#include "powerflow/powerflow.h" // NR_busdata
#include "powerflow/link.h" // SubNode
#include "powerflow/node.h" // SubNode

//Based on solver_nr.h on defition of BUSDATA
#define PHASE_NR_S 128
#define PHASE_NR_HOUSE 64
#define PHASE_NR_D 8
#define PHASE_NR_A 4
#define PHASE_NR_B 2
#define PHASE_NR_C 1

EXPORT_CREATE(uot_network_exporter);
EXPORT_INIT(uot_network_exporter);
EXPORT_COMMIT(uot_network_exporter);
EXPORT_SYNC(uot_network_exporter);


CLASS *uot_network_exporter::oclass = NULL;
uot_network_exporter *uot_network_exporter::defaults = NULL;

uot_network_exporter::uot_network_exporter(MODULE *module)
{
    if (oclass != NULL) {
        exception("cannot register class more than once");
    }

    oclass = gld_class::create(module, "uot_network_exporter", sizeof(uot_network_exporter),
                               PC_AUTOLOCK | PC_OBSERVER |  PC_PRETOPDOWN | PC_BOTTOMUP | PC_POSTTOPDOWN);

    if (oclass == NULL) {
        exception("uot_network_exporter class registration failed");

    }

    // Tests verified that output from this class can be used to replicate Gridlab-D power flow solutions and IEEE
    // test feeder specifications.
    oclass->trl = TRL_DEMONSTRATED;

    if (gl_publish_variable(oclass,
                            PT_char1024, "file_name_node_data", PADDR(file_name_node_data),
                            PT_DESCRIPTION, "file name for node data",
                            PT_char1024, "file_name_branch_data",
                            PADDR(file_name_branch_data), PT_DESCRIPTION,
                            "file name for branch data",
                            PT_enumeration, "admittance_change_output",PADDR(admittance_change_output),PT_DESCRIPTION,"Designation of output type for notifying changes in admittance.",
                            PT_KEYWORD, "NONE", (enumeration)NONE,
                            PT_KEYWORD, "INFO", (enumeration)INFO,
                            PT_KEYWORD, "WARNING", (enumeration)WARNING,
                            PT_KEYWORD, "ERROR", (enumeration)ERROR,
                            NULL) < 1)
        throw "unable to publish uot_network_exporter variables";

    // this should not be done if virtual functions are used
    memset(defaults = this, 0, sizeof(*this));
}

int uot_network_exporter::create(void) {
    if (solver_method != SM_NR) {
        gl_error("uot_network_exporter only works for NR solver.");
        return FAILED;
    }

    memcpy(this, defaults, sizeof(*this));

    // Member defaults
    file_name_node_data.get_string()[0] = '\0';
    file_name_branch_data.get_string()[0] = '\0';

    admittance_change_output = ERROR;

    flag_admittance_change_acknowledged = false;

    is_first_run = true;

    return SUCCESS; // return FAILED on create error
}

int uot_network_exporter::init(OBJECT *parent) {
    return SUCCESS; // return FAILED on create error
}

TIMESTAMP uot_network_exporter::presync(TIMESTAMP t0) {
    //gl_output("DEBUG: presync.");
    this->admittance_change_watchdog(NR_admit_change);

    return TS_NEVER;
}

TIMESTAMP uot_network_exporter::sync(TIMESTAMP t0) {
    //gl_output("DEBUG: sync.");
    this->admittance_change_watchdog(NR_admit_change);

    return TS_NEVER; // return TS_INVALID on failure
}

TIMESTAMP uot_network_exporter::postsync(TIMESTAMP t0) {
    //gl_output("DEBUG: postsync.");
    this->admittance_change_watchdog(NR_admit_change);
    return TS_NEVER;
}

TIMESTAMP uot_network_exporter::commit(TIMESTAMP t1, TIMESTAMP t2) {

    if (is_first_run) {
        this->development_status_check(NR_bus_count, &NR_powerflow, NR_busdata);

        if (file_name_node_data.get_string()[0] != '\0') {
            this->print_node_information(NR_bus_count, NR_busdata);
        }
        if (file_name_branch_data.get_string()[0] != '\0') {
            this->print_branch_information(NR_branch_count,NR_branchdata,NR_busdata);
        }
        is_first_run = false;
    }

    return TS_NEVER; // return TS_INVALID on failure
}

void uot_network_exporter::print_branch_information(unsigned int branch_count, BRANCHDATA *branch, BUSDATA *bus)
{
    // Prints branch data to a text file named after file_name_branch_data
    // Using CSV format with header Name,From,To,PhaseA,PhaseB,PhaseC,PhaseD,PhaseS,LinkType,Yfrom,Yto,YSfrom,YSto
    // Matrices Yfrom,Yto,YSfrom,YSto are printed in Matlab format (A = [1,2;3,4])
    std::ofstream file_stream;
    file_stream.open(file_name_branch_data);

    const int n_phase = 3;

    if (file_stream.is_open()) {
        // Print enough digits of floating point numbers so that we do not
        // lose information
        file_stream.precision(std::numeric_limits<double>::digits10 + 1);
        file_stream << "Name,From,To,";
        this->phase_header_to_stream(file_stream);
        file_stream << ",LinkType,Yfrom,Yto,YSfrom,YSto";
        file_stream << '\n';

        for (int i_branch = 0; i_branch < branch_count; ++i_branch) {

            file_stream << branch[i_branch].name << ',';

            const int from_bus_index = branch[i_branch].from;
            const int to_bus_index = branch[i_branch].to;

            file_stream << bus[from_bus_index].name << ',';
            file_stream << bus[to_bus_index].name << ',';

            this->phase_NR_to_stream(file_stream, branch[i_branch].phases);
            file_stream << ',';

            this->link_type_to_stream(file_stream, branch[i_branch].lnk_type, bus[to_bus_index].phases);
            file_stream << ',';

            // Recall that Yfrom,Yto,YSfrom,YSto are all 3x3 matrices
            matrix_to_stream(file_stream,branch[i_branch].Yfrom,n_phase,n_phase);
            file_stream << ',';
            matrix_to_stream(file_stream,branch[i_branch].Yto,n_phase,n_phase);
            file_stream << ',';
            matrix_to_stream(file_stream,branch[i_branch].YSfrom,n_phase,n_phase);
            file_stream << ',';
            matrix_to_stream(file_stream,branch[i_branch].YSto,n_phase,n_phase);

            if (i_branch != branch_count - 1) {
                file_stream << '\n';
            }
        }

        file_stream.close();

    } //file_stream.is_open()
    else {
        if (file_name_branch_data.get_string()[0] != '\0'){
            GL_THROW("Could not open file to print_branch_information.");
        }
    }
}

// Print matrix to stream using Matlab notation
void uot_network_exporter::matrix_to_stream(std::ofstream &stream, complex * X, int M, int N)
{
    // Outputs a matrix to a stream in Matlab format (A = [1,2;3,4])

    stream << "[";
    for(int m = 0; m < M; ++m) {
        for(int n = 0; n < N; ++n) {
            stream << X[m*M + n];
            if(n != N - 1) {
                stream << ", ";
            }
        }

        if(m != M - 1) {
            stream << "; ";
        }
    }
    stream << "]";
}

void uot_network_exporter::print_node_information(unsigned int bus_count, BUSDATA *bus)
{
    // Prints node data to a text file named after file_name_node_data
    // Using CSV format with header Name,Volt_base,PhaseA,PhaseB,PhaseC,PhaseD,PhaseS,Is_Swing

    std::ofstream file_stream;

    file_stream.open(file_name_node_data);

    if (file_stream.is_open()) {
        // Print enough digits of floating point numbers so that we do not
        // lose information
        file_stream.precision(std::numeric_limits<double>::digits10 + 1);

        file_stream << "Name,Volt_base,";
        this->phase_header_to_stream(file_stream);
        file_stream << ",Is_Swing";
        file_stream << '\n';

        for (int i_bus = 0; i_bus < bus_count; ++i_bus) {

            file_stream << bus[i_bus].name << ',';

            file_stream << bus[i_bus].volt_base << ',';

            this->phase_NR_to_stream(file_stream,bus[i_bus].phases);

            file_stream << ',' << bus[i_bus].swing_functions_enabled;

            file_stream << '\n';
        }

        file_stream.close();

    } //file_stream.is_open()
    else {
        GL_THROW("Could not open file to print_phase_information.");
    }
}

void uot_network_exporter::phase_header_to_stream(std::ofstream &stream)
{
    stream << "PhaseA,PhaseB,PhaseC,PhaseD,PhaseS";
}

void uot_network_exporter::development_status_check(unsigned int bus_count, NR_SOLVER_STRUCT *powerflow_values, BUSDATA *bus)
{
    // Goes through NR_powerflow and NR_busdata to check that the model does not include untested features
    // Throws a warning if it does.

    for (int i_bus = 0; i_bus < bus_count; ++i_bus) {
        node *node_ptr = OBJECTDATA(bus[i_bus].obj, node);
        if (node_ptr->house_present) {
            gl_warning("Model includes a node with a house. Functionality has not been tested.");
        }
        if (node_ptr->bustype == 1) {
            gl_warning("Model includes a PV node. Functionality has not been tested.");
        }
    }
}


void uot_network_exporter::admittance_change_watchdog(bool admittance_change)
{
    // Watches admittance_change to detect changes in the admittance matrix. If one
    // is detected, action is taken according to admittance_change_output.

    // In the first run, we do not care about admittance changes since we do not export the data until commit
    if(!is_first_run & !flag_admittance_change_acknowledged && admittance_change) {
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


void uot_network_exporter::phase_NR_to_stream(std::ofstream &stream, unsigned char phases)
{
    // Prints phases in NR format to a stream. Note that the NR phase format used by solver_nr is not the same as
    // the standard one in powerflow_object.h

    const bool phase_a = (phases & PHASE_NR_A) == PHASE_NR_A;
    const bool phase_b = (phases & PHASE_NR_B) == PHASE_NR_B;
    const bool phase_c = (phases & PHASE_NR_C) == PHASE_NR_C;
    const bool phase_d = (phases & PHASE_NR_D) == PHASE_NR_D;
    const bool phase_s = (phases & PHASE_NR_S) == PHASE_NR_S;

    stream << phase_a << ',' << phase_b << ','
           << phase_c << ',' << phase_d << ',' << phase_s;
}

void uot_network_exporter::link_type_to_stream(std::ofstream &stream, unsigned char branch_lnk_type, unsigned char bus_to_phases)
{
    // Outputs the link type in string format to a stream

    std::string  link_type_name;

    // From solver_nr.h
    // 0 = UG/OH line, 1 = Triplex line, 2 = switch, 3 = fuse, 4 = transformer, 5 = sectionalizer, 6 = recloser
    switch (branch_lnk_type) {
        case 0:
            link_type_name = "line";
            break;

        case 1:
            link_type_name = "triplex_line";
            break;

        case 2:
            link_type_name = "switch";
            break;

        case 3:
            link_type_name = "fuse";
            break;

        case 4:
            // Based on solver_nr.cpp line 1137. (bus_to->phases & 0x20) == 0x20) means that the bus_to is the to side
            // from an SPCT.
            if ((bus_to_phases & 0x20) == 0x20) {
                link_type_name = "spct";
            } else {
                link_type_name = "transformer";
            }
            break;

        case 5:
            link_type_name = "sectionalizer";
            break;

        case 6:
            link_type_name = "recloser";
            break;

        default:
            GL_THROW("Unrecognized link_type_name");
    }

    stream << link_type_name;
}

