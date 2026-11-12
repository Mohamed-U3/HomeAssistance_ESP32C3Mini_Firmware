/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "ESP32-C3 Smart Relay Controller", "index.html", [
    [ "ESP32-C3 8-Channel Smart Relay Controller (Home Assistant + Web UI)", "index.html", "index" ],
    [ "System Architecture &amp; Presentation Diagrams", "md_docs_2_s_y_s_t_e_m___a_r_c_h_i_t_e_c_t_u_r_e.html", [
      [ "1. High-Level Hardware &amp; Software Block Diagram", "md_docs_2_s_y_s_t_e_m___a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md23", null ],
      [ "2. Module Include &amp; Dependency Graph", "md_docs_2_s_y_s_t_e_m___a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md25", null ],
      [ "3. Wi-Fi Connection, 2-Minute Failover &amp; Portal State Machine", "md_docs_2_s_y_s_t_e_m___a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md27", null ],
      [ "4. Multi-Source Relay Control &amp; Real-Time Synchronization", "md_docs_2_s_y_s_t_e_m___a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md29", null ],
      [ "5. Non-Blocking FreeRTOS MQTT Probe &amp; LWT Flow", "md_docs_2_s_y_s_t_e_m___a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md31", null ],
      [ "6. OTA Firmware Update Workflows", "md_docs_2_s_y_s_t_e_m___a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md33", null ]
    ] ],
    [ "Product Catalog &amp; Technical Datasheet", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html", [
      [ "U3 Smart Home System v1.0", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md35", [
        [ "ESP32-C3 8-Channel Smart Relay Controller", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md36", null ]
      ] ],
      [ "1. Product Overview", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md38", null ],
      [ "2. Hardware Gallery &amp; Visual Inspection", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md40", [
        [ "2.1 3D Perspective Views", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md41", null ],
        [ "2.2 Top &amp; Bottom Orthographic Layout", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md42", null ]
      ] ],
      [ "3. Key Features &amp; Highlights", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md44", null ],
      [ "4. Technical Specifications", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md46", [
        [ "4.1 Electrical Characteristics", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md47", null ],
        [ "4.2 Wireless &amp; Network Specifications", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md48", null ],
        [ "4.3 Mechanical &amp; Environmental Specifications", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md49", null ]
      ] ],
      [ "5. Board Components &amp; Interface Mapping", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md51", [
        [ "5.1 Onboard Connectors &amp; Terminals", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md52", null ],
        [ "5.2 Microcontroller &amp; Active Semiconductor Devices", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md53", null ]
      ] ],
      [ "6. Software &amp; Integration Architecture", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md55", [
        [ "6.1 Home Assistant Integration (Zero YAML)", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md56", [
          [ "MQTT Topic Structure", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md57", null ]
        ] ],
        [ "6.2 Responsive WebSocket Web Dashboard", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md58", null ]
      ] ],
      [ "7. Typical Application Scenarios", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md60", null ],
      [ "8. Wiring &amp; Installation Guide", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md62", null ],
      [ "9. Ordering &amp; Support Information", "md_docs_2_p_r_o_d_u_c_t___c_a_t_a_l_o_g.html#autotoc_md64", null ]
    ] ],
    [ "Topics", "topics.html", "topics" ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", null ],
        [ "Variables", "functions_vars.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Functions", "globals_func.html", null ],
        [ "Variables", "globals_vars.html", null ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerations", "globals_enum.html", null ],
        [ "Enumerator", "globals_eval.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"annotated.html"
];

const SYNCONMSG = 'click to disable panel synchronization';
const SYNCOFFMSG = 'click to enable panel synchronization';
const LISTOFALLMEMBERS = 'List of all members';