// ** LICENSE NOTICE **
// Parts of this file are based on the script "FAST_Prog.cpp" distributed with OpenFAST v3.5.0 under a Apache 2.0 license.
// The following functions were taken from previous work: checkFileExists(), readTurbineData(), readInputFile()
// The following functions were adapted or extended: main()
// The original code can be found here: https://github.com/OpenFAST/openfast/tree/v3.5.0/glue-codes/openfast-cpp/src/FAST_Prog.cpp

#include "/home/leonard/Tools/openfast/install/include/OpenFAST.H" // use for local installation
//#include <OpenFAST.H> // use for global installation
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <mpi.h> 
#include <sstream>
#include <precice/precice.hpp>
#include <map>
#include <string>


// --------------- Helper functions to read input files for OpenFAST -----------------------

inline bool checkFileExists(const std::string& name) {
    struct stat buffer;   
    return (stat (name.c_str(), &buffer) == 0); 
}

void readTurbineData(int iTurb, fast::fastInputs & fi, YAML::Node turbNode) {
    //Read turbine data for a given turbine using the YAML node
    fi.globTurbineData[iTurb].TurbID = turbNode["turb_id"].as<int>();
    fi.globTurbineData[iTurb].FASTInputFileName = turbNode["FAST_input_filename"].as<std::string>();
    fi.globTurbineData[iTurb].FASTRestartFileName = turbNode["restart_filename"].as<std::string>();
    if (turbNode["turbine_base_pos"].IsSequence() ) {
        fi.globTurbineData[iTurb].TurbineBasePos = turbNode["turbine_base_pos"].as<std::vector<double> >();
    }
    if (turbNode["turbine_hub_pos"].IsSequence() ) {
        fi.globTurbineData[iTurb].TurbineHubPos = turbNode["turbine_hub_pos"].as<std::vector<double> >();
    }
    fi.globTurbineData[iTurb].numForcePtsBlade = turbNode["num_force_pts_blade"].as<int>();
    fi.globTurbineData[iTurb].numForcePtsTwr = turbNode["num_force_pts_tower"].as<int>();
    if (turbNode["nacelle_cd"]) fi.globTurbineData[iTurb].nacelle_cd = turbNode["nacelle_cd"].as<float>();
    if (turbNode["nacelle_area"]) fi.globTurbineData[iTurb].nacelle_area = turbNode["nacelle_area"].as<float>();
    if (turbNode["air_density"]) fi.globTurbineData[iTurb].air_density = turbNode["air_density"].as<float>();
}

void readInputFileFAST(fast::fastInputs & fi, std::string cInterfaceInputFile, double * tEnd) {

    fi.comm = MPI_COMM_WORLD;

    // Check if the input file exists and read it
    if ( checkFileExists(cInterfaceInputFile) ) {

        YAML::Node cDriverInp = YAML::LoadFile(cInterfaceInputFile);

        fi.nTurbinesGlob = cDriverInp["nTurbinesGlob"].as<int>();

        if (fi.nTurbinesGlob > 0) {

            if(cDriverInp["dryRun"]) {
                fi.dryRun = cDriverInp["dryRun"].as<bool>();
            } 

            if(cDriverInp["debug"]) {
                fi.debug = cDriverInp["debug"].as<bool>();
            } 

            if(cDriverInp["simStart"]) {
                if (cDriverInp["simStart"].as<std::string>() == "init") {
                    fi.simStart = fast::init;
                } else if(cDriverInp["simStart"].as<std::string>() == "trueRestart") {
                    fi.simStart = fast::trueRestart;
                } else if(cDriverInp["simStart"].as<std::string>() == "restartDriverInitFAST") {
                    fi.simStart = fast::restartDriverInitFAST;
                } else {
                    throw std::runtime_error("simStart is not well defined in the input file");
                }
            }

            fi.tStart = cDriverInp["tStart"].as<double>();
            *tEnd = cDriverInp["tEnd"].as<double>();
            fi.nEveryCheckPoint = cDriverInp["nEveryCheckPoint"].as<int>();
            fi.dtFAST = cDriverInp["dtFAST"].as<double>();
            fi.tMax = cDriverInp["tMax"].as<double>(); 
            // tMax is the total duration to which you want to run FAST.
            // This should be the same or greater than the max time given in the FAST fst file.
            // Choose this carefully as FAST writes the output file only at this point if you choose the binary file output.

            if(cDriverInp["superController"]) {
                fi.scStatus = cDriverInp["superController"].as<bool>();
                fi.scLibFile = cDriverInp["scLibFile"].as<std::string>();
            }

            fi.globTurbineData.resize(fi.nTurbinesGlob);
            for (int iTurb=0; iTurb < fi.nTurbinesGlob; iTurb++) {
                if (cDriverInp["Turbine" + std::to_string(iTurb)]) {
                    readTurbineData(iTurb, fi, cDriverInp["Turbine" + std::to_string(iTurb)] );
                } else {
                    throw std::runtime_error("Node for Turbine" + std::to_string(iTurb) + " not present in input file or I cannot read it");
                }
            }

        } else {
            throw std::runtime_error("Number of turbines <= 0 ");
        }

    } else {
        throw std::runtime_error("Input file " + cInterfaceInputFile + " does not exist or I cannot access it");
    }
}


void readInputFilePrecice(YAML::Node & preciceInp, std::string preciceInputFile) {

    // Check if the input file exists and read it
    if ( checkFileExists(preciceInputFile) ) {

        preciceInp = YAML::LoadFile(preciceInputFile);

    } else {
        throw std::runtime_error("Input file " + preciceInputFile + " does not exist or I cannot access it");
    }
}

// --------------- main function ------------------------------------------------------
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cout << "Usage: ./openfast-adapter preciceInputFile openfastInputFile\n\n";
        std::cout << "Parameter description\n";
        std::cout << "  preciceInputFile: Path and filename of preCICE input file (.yaml)\n";
        std::cout << "  openfastInputFile: Path and filename of OpenFAST C++ configuration (.yaml)\n\n\n";
    }
    
    
    // --------------- Initialize OpenFAST ------------------------------------------------------
    
    int iErr;
    int nProcs ;
    int rank;
    std::vector<double> point (3, 0.0); 
    double time_cp;
    
    // variables for data exchange
    std::vector<double> force(3, 0.0);
    std::vector<double> coords(3);
    std::vector<double> velocity(3, 10.0);
    int iNode = 0;
    int iTurb = 0;
    
    // preCICE initializes MPI too --> double initialization might cause problems
    iErr = MPI_Init(NULL,NULL);
    
    // preCICE controls the time now
    double tEnd ; // This doesn't belong in the FAST - C++ interface 
    //int ntEnd ; // This doesn't belong in the FAST - C++ interface
    

    std::string cDriverInputFile=argv[2];
    fast::OpenFAST FAST;
    fast::fastInputs fi ;
    try {
        readInputFileFAST(fi, cDriverInputFile, &tEnd);
    } catch( const std::runtime_error & ex) {
        std::cerr << ex.what() << std::endl ;
        std::cerr << "Program quitting now" << std::endl ;
        return 1;
    }

    FAST.setInputs(fi);
    FAST.allocateTurbinesToProcsSimple(); 
    // Or allocate turbines to procs by calling "setTurbineProcNo(iTurbGlob, procId)" for turbine.

    FAST.init();
    
    if (FAST.isTimeZero()) FAST.solution0();
    
    // --------------- Initialize preCICE ------------------------------------------------------
    
    YAML::Node preciceInp;
    std::string preciceInputFile = argv[1];
    try {
        readInputFilePrecice(preciceInp, preciceInputFile);
    } catch( const std::runtime_error & ex) {
        std::cerr << ex.what() << std::endl ;
        std::cerr << "Program quitting now" << std::endl ;
        return 1;
    }
    using namespace precice;
    
    int commRank = 0;
    int commSize = 1;

    std::string configFileName  = preciceInp["preciceConfig"].as<std::string>();
    std::string solverName      = preciceInp["solverName"].as<std::string>();
    std::string meshReadName    = preciceInp["meshReadName"].as<std::string>();
    std::string meshWriteName   = preciceInp["meshWriteName"].as<std::string>();
    std::string dataReadName    = preciceInp["dataReadName"].as<std::string>();
    std::string dataWriteName   = preciceInp["dataWriteName"].as<std::string>();
    bool debugMode              = preciceInp["debug"].as<bool>();

    Participant participant(solverName, configFileName, commRank, commSize);
    
    // --------------- Set up the meshes ------------------------------------------------------

    int dimensions              = participant.getMeshDimensions(meshReadName);
    int numberOfForceVertices   = FAST.get_numForcePts(iTurb);
    int numberOfVelVertices     = FAST.get_numVelPts(iTurb);

    std::vector<double> readData(numberOfVelVertices * dimensions);
    std::vector<double> writeData(numberOfForceVertices * dimensions);
    std::vector<double> verticesVel(numberOfVelVertices * dimensions);
    std::vector<double> verticesForce(numberOfForceVertices * dimensions);
    std::vector<int>    vertexReadIDs(numberOfVelVertices);
    std::vector<int>    vertexWriteIDs(numberOfForceVertices);
    
    std::vector<double> nodeVelocity(dimensions);
    std::vector<double> nodeForce(dimensions);
    
    //Initialize force mesh with data from FAST
    for (int i = 0; i < numberOfForceVertices; i++) {
      for (int j = 0; j < dimensions; j++) {
        // positions
        FAST.getForceNodeCoordinates(coords, i, iTurb);
        verticesForce.at(j + dimensions * i)  = coords[j];
        // force
        FAST.getForce(force, i, iTurb);
        writeData.at(j + dimensions * i) = force[j];
      }
    }
    
    //Initialize velocity mesh with initial data from preCICE or manually
    for (int i = 0; i < numberOfVelVertices; i++) {
      for (int j = 0; j < dimensions; j++) {
        // positions
        FAST.getVelNodeCoordinates(coords, i, iTurb);
        verticesVel.at(j + dimensions * i)  = coords[j];
        // velocity - How to initialize? Data should come from Fluid Participant. Initialize via preCICE, but probably later in the code
        readData.at(j + dimensions * i)  = 0.0; //initialize velocity manually for now
      }
    }

    participant.setMeshVertices(meshWriteName, verticesForce, vertexWriteIDs);
    participant.setMeshVertices(meshReadName, verticesVel, vertexReadIDs);
    
    if (participant.requiresInitialData()) {
        participant.writeData(meshWriteName, dataWriteName, vertexWriteIDs, writeData);    
    }
    
    participant.initialize();
    
    if (debugMode){
        // Print data from one node to check plausability
        std::cout << "Velocity in node four of blade 1: " + std::to_string(readData[9]) + "   " + std::to_string(readData[10]) + "   " + std::to_string(readData[11]) + "\n";  
        std::cout << "Force in node four of blade 1: " + std::to_string(writeData[9]) + "   " + std::to_string(writeData[10]) + "   " + std::to_string(writeData[11]) + "\n";
    }
    
    
    double time = 0.0;
    double dt = participant.getMaxTimeStepSize();

    
    // --------------- Main loop ------------------------------------------------------
    while (participant.isCouplingOngoing()) {

        if (participant.requiresWritingCheckpoint()) {
          std::cout << "Not implemented: Writing iteration checkpoint\n";
          time_cp = time;
        }
        
        
        // read data from Fluid
        
        participant.readData(meshReadName, dataReadName, vertexReadIDs, dt, readData);
        
        if (debugMode){
            std::cout << "Velocity in node four of blade 1: " + std::to_string(readData[9]) + "   " + std::to_string(readData[10]) + "   " + std::to_string(readData[11]) + "\n";  
        }
        
        
        // set data in FAST
        for (int iVertice = 0; iVertice < numberOfVelVertices; iVertice++) {
            for (int iDim = 0; iDim < dimensions; iDim++) {
                nodeVelocity[iDim] = readData.at(iDim + dimensions * iVertice);
            }
            FAST.setVelocity(nodeVelocity, iVertice, iTurb);
        }
   
        // calculate next time step
        FAST.step();
        
        // get data from FAST
        for(int i=0; i < numberOfForceVertices; i++) {
           FAST.getForce(nodeForce, i, iTurb);
           for (int iDim = 0; iDim < dimensions; iDim++) {
              writeData.at(iDim + dimensions * i) = nodeForce[iDim];
           }
        }
        
        if (debugMode){
        std::cout << "Force in node four of blade 1: " + std::to_string(writeData[9]) + "   " + std::to_string(writeData[10]) + "   " + std::to_string(writeData[11]) + "\n"; 
        }
        
        
        // TODO: Update positions of vertices
        // It is possible to get the velocity node co-ordinates with: FAST.getVelNodeCoordinates(currentCoords, i, iTurb);
        // It is possible to get the force node co-ordinates with: FAST.getForceNodeCoordinates(currentCoords, i, iTurb);
        
        // write data
        participant.writeData(meshWriteName, dataWriteName, vertexWriteIDs, writeData);


        // advance the simulation
        participant.advance(dt);

     if (participant.requiresWritingCheckpoint()) {
          std::cout << "Not implemented: Reading iteration checkpoint\n";
          time = time_cp;
    
        } else {
          std::cout << "Advancing in time\n";
          time = time + dt;
        }
    }
    
    std::cerr << "Close FAST" << std::endl;
    FAST.end();
    participant.finalize();

    return 0;

}
