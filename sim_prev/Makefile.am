AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = simulator simdebug

simulator_SOURCES = 				 		 	 \
					coresim/main.cpp 			 \
					coresim/debug.cpp 			 \
					coresim/node.cpp 			 \
					coresim/queue.cpp 			 \
					coresim/packet.cpp 		 	 \
					coresim/event.cpp 			 \
					coresim/topology.cpp 		 \
					coresim/flow.cpp 			 \
					coresim/random_variable.cpp  \
					coresim/channel.cpp  		 \
					ext/factory.cpp 		 	 \
					ext/veritasflow.cpp         \
					ext/wfQueue.cpp			     \
					run/params.cpp 		 	 	 \
					run/stats.cpp 			   	 \
					run/flow_generator.cpp       \
					run/experiment.cpp 

simulator_CXXFLAGS = -g -O3 -gdwarf-2 -Wall -std=c++0x 

simdebug_SOURCES = $(simulator_SOURCES)

simdebug_CXXFLAGS  = -g -O0 -gdwarf-2 -Wall -std=c++0x 

#CFLAGS = -g -O3 -gdwarf-2 -Wall -std=c++0x 
#CXXFLAGS = -g -O3 -gdwarf-2 -Wall -std=c++0x 

