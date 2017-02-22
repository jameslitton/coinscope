# coinscope
An observation and testing framework for bitcoin or bitcoin based altcoins

This software was written to allow for easy measurement and communication with the entire set of bitcoin and bitcoin based altcoins and thus far has culminated in a paper on the topic.

The paper was written in collaboration with Andrew Miller, James Litton, Andrew Pachulski, Neal Gupta,
Dave Levin, Neil Spring, and Bobby Bhattacharjee. The project page is available here. http://cs.umd.edu/projects/coinscope/.

I have been waiting to open source this for quite a while and have delayed until I documented it. This seems it may never come without nudges by others, so I am releasing it now with sparse documentation. I will fill in the documentation in response to questions or in the unlikely event I get bored.

## Design

The design breaks down the major components of the system into separate processes. This design was meant to make splitting the design across machines trivial, as well as allowing components to scale independent of another.

Components communicate with one another over sockets, either unix or tcp. The components are as follows:

## Connector

The connector, as the name suggests, connects to other bitcoin nodes and plays the protocol. It does not send any unnecessary traffic on its own, but will respond to pings and attempt to keep connections alive. To direct the connector to send messages to other nodes, a client can inject commands into it from opening a socket on the control path. C and python libraries are provided to speak the binary communication protocol it expects.

This component is single-threaded. In our experiments it was able to communicate with and attempt to connect to the entire discoverable bitcoin network on a single thread. The horde branch allows for multiple connectors to run under a multiplexing master connecto.

The connector logs all of its messages to a separate component, the log server. These logs can be viewed with a log client.

## The Log Server

The connector and other components can connect to the log server and inject log events into the system. Logging clients can connect to the log server and receive messages that they are interested in. The log server will manage multiplexing the logging stream to however many clients are interested in it.

## Log Clients

These components connect to the logging server and view the logging stream. The verbatim logger just captures the raw output of the log server, which can be used to store long-term logs. The console logger prints a plaintext version of the log to the console. 

## Clients

Clients connect to the other components as needed and inject commands into the connector. A client may request the connector to connect to a bitcoin node, for instance, or broadcast a message to all nodes. The client may then wait and watch the network traffic by connection to the log server to see how its commands perturbed the network.

## Getting started

At minimum, building the software requires libconfig++, libev-dev, Boost program options, and a C++ compiler. Once those are installed, go into the root directory and type make.

Any missing dependencies should hopefully be obvious by the build error. 

You will want to modify the configuration in netmine.cfg appropriately. The configuration is mostly self-explanatory and commented.

To run the software, it's easiest to start programs (which reside in their subdirectories) in the following order: the log server, any log clients (e.g., the console client), the connector, and then finally any clients for the connector.

Generally speaking if you do things in the "wrong" order it should do sensible things (e.g., just log to the console if the logserver isn't up or yell at you), but this makes the most sense.

If you are logging everything to disk with the verbatim logger, you'll probably want to rotate logs eventually. This requires a two step process to signal to the verbatim logger that this is about to occur so it doesn't write incomplete logging onto the new file. A logrotate script that does this is in the tools section under verbatim-rotate.cfg

For clients, the place to start may be the connect client. Just modify the source code to give it the address of a bitcoin node (or run one on 127.0.0.1) and run this script. It should connect to the connector, send the connect request, and then output the results of the request. Other clients are available as examples. 

If you prefer to use Python instead of C, python libraries are in the libraries/python directory.

## Horde Branch

As mentioned above, the horde is a system that allows for multiple connector instances. It is less tested than the master branch, but at some point in the future the intention is that it will be merged with the master branch. Bug reports/fixes welcome.



