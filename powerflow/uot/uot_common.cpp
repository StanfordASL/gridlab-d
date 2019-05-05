// $Id: uot_common.cpp$
//	Copyright (C) 2018 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)

#include "uot_common.h"

// Operator to print complex numbers
std::ostream &operator<<(std::ostream & out_stream, complex & z){
    out_stream << z.Re();

    //If im is negative, minus sign appears automatically
    if (!std::signbit(z.Im())) {
        out_stream << '+';
    }

    out_stream << z.Im() << 'i';
}

// Convert TIMESTAMP to string
std::string timestamp_to_string(TIMESTAMP ts)
{
    const int n_char_buf = 100;
    char* char_buf = new char[n_char_buf];
    gl_strftime(ts,char_buf,n_char_buf);

    std::string ts_str(char_buf,n_char_buf);

    delete[] char_buf;

    return ts_str;
}
