AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = simulator simdebug

simulator_SOURCES =                              \
					coresim/simulator.cpp        \
					coresim/agg_channel.cpp      \
					coresim/channel.cpp          \
					coresim/debug.cpp            \
					coresim/event.cpp            \
					coresim/flow.cpp             \
					coresim/nic.cpp              \
					coresim/node.cpp             \
					coresim/queue.cpp            \
					coresim/packet.cpp           \
					coresim/random_variable.cpp  \
					coresim/topology.cpp         \
					ext/d3_queue.cpp             \
					ext/d3_flow.cpp              \
					ext/factory.cpp              \
					ext/homa_channel.cpp		 \
					ext/homa_flow.cpp			 \
					ext/homa_host.cpp			 \
					ext/aequitas_flow.cpp         \
					ext/pfabric_flow.cpp         \
					ext/pfabric_queue.cpp        \
					ext/pdq_queue.cpp            \
					ext/pdq_flow.cpp             \
					ext/qjump_channel.cpp        \
					ext/qjump_flow.cpp           \
					ext/qjump_host.cpp           \
					ext/wf_queue.cpp             \
					run/experiment.cpp           \
					run/flow_generator.cpp       \
					run/params.cpp

simulator_CXXFLAGS = -g -O3 -gdwarf-2 -Wall -std=c++0x 

simdebug_SOURCES = $(simulator_SOURCES)

simdebug_CXXFLAGS  = -g -O0 -gdwarf-2 -Wall -std=c++0x 

#CFLAGS = -g -O3 -gdwarf-2 -Wall -std=c++0x 
#CXXFLAGS = -g -O3 -gdwarf-2 -Wall -std=c++0x 

