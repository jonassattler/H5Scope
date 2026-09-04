# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# The postprocessing pipeline: the array engine, the six operations, and the
# computed data source the views read the result through.
#
# Between h5core and gui in the stack. It holds no Qt type of its own -- there
# is no QObject here and AUTOMOC is off -- but it links Qt6::Core, because the
# subscript grammar it shares with the table's slice line is written in QString
# and its error messages are the ones the bar prints.
add_library(postproc STATIC
  Array.cpp
  Subscripts.cpp
  Operations.cpp
  Pipeline.cpp
  ComputedDataset.cpp
)

target_include_directories(postproc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(postproc PUBLIC h5core Qt6::Core)
target_compile_features(postproc PUBLIC cxx_std_20)

# Nothing here is a Qt type; the moc pass over it is pure cost.
set_target_properties(postproc PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
