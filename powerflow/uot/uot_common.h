// $Id: uot_common.h$
//	Copyright (C) 2018 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)
#ifndef _UOT_COMMON_H
#define _UOT_COMMON_H

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

#include "gldcore/gridlabd.h"

//This file implements common functionality of the GridLAB-D - Unbalanced OPF Toolkit Interface

struct zip_load_struct
{
    // Struct to represent ZIP loads

    const int n_phase = 3;

    std::vector<complex> S_d = std::vector<complex>(n_phase,0);
    std::vector<complex> S_y = std::vector<complex>(n_phase,0);

    std::vector<complex> Y_d = std::vector<complex>(n_phase,0);
    std::vector<complex> Y_y = std::vector<complex>(n_phase,0);

    std::vector<complex> I_d = std::vector<complex>(n_phase,0);
    std::vector<complex> I_y = std::vector<complex>(n_phase,0);
};

// Operator to print complex numbers
std::ostream &operator<<(std::ostream & out_stream, complex & z);

// Convert TIMESTAMP to string
std::string timestamp_to_string(TIMESTAMP ts);

#endif //_UOT_COMMON_H
