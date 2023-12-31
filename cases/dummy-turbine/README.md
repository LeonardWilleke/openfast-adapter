## Setup

This case couples a single NREL5MW turbine with a fixed rotor to a dummy fluid solver. It is very handy to get an insight into the C++ APIs of preCICE and OpenFAST, as well as for debugging.

Note the use of 3 different meshes here: The dummy solver has one mesh, while OpenFAST uses two different meshes for Force and Velocity data. It has the capacity to map between them internally and present only the Force mesh to a coupled solver. However, we want preCICE to do this mapping for us. This results in the coupling scheme visualized in the figure below: The `solid` participant (OpenFAST) provides two meshes different meshes for the read and write data. The fluid participant provides one mesh and maps the read and write data accordingly.

![Coupling scheme](images/config.png)

## Available solvers

- *openfast*: An OpenFAST simulation of a single NREL 5MW turbine coupled to preCICE by the code `openfast-adapter`. The input files `preciceInput.yaml` and `openfastInput.yaml` specify the simulation case by pointing to other files such as `nrel5mw.fst`.
- *fluid*: A dummy participant `fluid-solver` which creates a mesh, writes velocity data on it and reads force data from OpenFAST

## Compiling the code

To compile `fluid-solver` with `cmake`, open a terminal and run:

```bash
cd fluid
cmake .
make
```

## Running the Simulation

Open two separate terminals and start both participants by calling:

```bash
cd openfast
./run.sh
```

and

```bash
cd fluid
./run.sh
```
