# octopuslb
Mirror for octopuslb

Octopus Load Balancer v1.14
Comments, criticisms, questions and suggestions <alreay1@gmail.com>
http://octopuslb.sourceforge.net/ 

Octopus is a general purpose TCP load balancer for Linux. It is intended to be placed between your cache and your application servers but is just as happy running at the front end of a system. 


Octopus boasts the following features;

* The Octopus core server runs as a single process giving the fastest possible response times.

* A separate process monitors application server health to automatically disable/enable servers if their state changes.

* Five different load balancing algorithms to choose from:
	- Least Load		(LL)
	- Least Connections	(LC)
	- Round Robin		(RR)
	- http URI Hashing	(HASH)
	- static URI Hashing (STATIC)

* Octopus uses shared memory (SHM) allowing a separate administration binary to dynamically reconfigure the running application.

* Administration binary runs as an interactive shell-type program with usage examples and help information.

* Environment cloning. Octopus has a unique feature which allows the administrator to test and compare (in real-time) an entirely separate environment in parallel to your normal production environment. This feature is known as 'cloning' and warrants a separate section in the FAQ which can be found below.

* Ability to place limits on maximum number of connections and maximum UNIX-load an application server will handle.

* Can run many instances of Octopus on the same host

* A SQUID-style configuration file.

* Can use separate network interfaces for listening port and outbound ports.

* Logging with 5 levels of verbosity.


Installation
------------

Please read the INSTALL file in the directory where the Octopus package was extracted for system requirements and installation instructions. This is not required if you used the RPM method of installation.

Configuration
-------------

After installation, Octopus will be ready to run without any modifications to the configuration file. It will bind to the IP address 0.0.0.0 (which means every IP address on the box) and the TCP port 1080. 
The configuration file itself has plenty of documentation around every directive and includes some common examples of configurations used for different types of sites.
Throughout the documentation we will use the following terminology:

Member - a normal server which forms part of your load balancing cluster. This will be your production environment.
Clone - a server which is part of a separate environment used for testing and comparison alongside the normal production environment.
Session - a session is created when Octopus receives a new request on it's listening TCP port. A session encompasses the client, server and clone network sockets as well as data buffers and session state info.


The configuration file is broken into a number of sections;

1) Network Configuration

This is where you specify TCP/IP information about how Octopus should bind to your network stack. It is also where you specify outbound IP addresses for member servers and clone servers. The number of file descriptors you wish to use can also be set here if you do not wish to use the system default.

2) Server Selection

This is where you specify the type of load balancing you would like to use. Some methods have additional options such as Least Load (LL) which can specify an offset to the collected SNMP UNIX-load based on the number of active sessions to that particular server. Also in this section is a directive to enable or disable cloning, the process of creating an identical request for a separate environment.

3) Miscellaneous Settings

In this section you can change the directory where Octopus places its SHM file, defaults around number of connections a server will accept and its maximum load. You also specify logfile location along with the option to use SYSLOG and the total session limit for the Octopus instance.

4) Monitor Process

The monitoring process checks server availability as well as SNMP load, and if using HASH method, handles rebalancing too. In this section you can specify the time period between runs of the monitor process, the socket timeout when determining the health of a member or clone server and the SNMP community string (SNMP password) for the servers.

5) Member and Clone configuration

This is where you put the members and clones that will be used in your load balancing cluster. 

Basic Operation
---------------

After making any required changes to the configuration file you can start the server like this:
$ octopuslb-server -c /usr/local/etc/octopuslb.conf

Octopus will complain if your config file is invalid or if it could not bind to the IP or TCP port or many other error conditions.


You can then connect using the admin interface
$ octopuslb-admin

You will be presented with the Octopus admin prompt, which is best viewed in a wide terminal window, and can now watch Octopus' operation in real-time. Type '?' <Enter> to see the list of available commands, the command you will probably use the most is the 'show' command.

Within the client interface you can change just about anything dynamically, the only exceptions are the file descriptor limit and session limit.


FAQ
---

Q. How do I run multiple instances of Octopus?
A. Create a new copy of the configuration file, modify the TCP/IP settings and any others, then start the octopuslb-server with the new configuration file as a parameter


Q. OK, done that, now how do I control both instances?
A. The octopuslb-admin will see that there are two separate instances and will give you a choice of which you wish to control. If you want to change instances within the admin program then enter 'scan' on the Octopus command-line.


Q. Why would I want to use the Clone mode?
A. If you have a separate (fully or semi-replicated) environment for testing, staging, or development you should consider using this powerful feature. Clone mode creates a copy of the client's request and sends this off to a server of type 'clone' as well as to the normal 'member' servers. In a webserver environment this means the client's HTTP request is sent to a normal 'member' server as well as being sent to a 'clone' server. The clone's response is binned by Octopus whilst the response from the 'member' is passed back to the client normally. Additional data sent by the client continues to be sent to the clone server so it can handle, for example, HTTP POST data too.
The advantage of using cloning is that this clone environment gets real-world, real-time data from your production environment allowing you to compare performance, and because the data from the clone environment is not passed back to the client you can make changes to your running clone systems without fear of impacting your front-end website. If you are having performance problems you can try changing a setting in clone and then compare the performance using SNMP load viewable within the client. 

For example: Your application servers are getting overloaded, you think you can fix the problem by increasing the amount of server processes available to your httpd servers. The issue only seems to occur under real-world load and if you make the change to production the problem could become worse. Would you take the risk without testing first? How can you really know the change will work if you can't replicate the problem accurately?
 
Using cloning means you can change the parameter in your clone environment and then monitor the clone servers to see if there is a difference in performance. If the load on the clone environment decreases then you know your fix works. If the load increases then it's back to the drawing board.


Q. I want to control the load balancer using scripts. How can I do this?
A. The admin binary allows you to run in non-interactive mode, commands that would normally be entered on the octopuslb-admin shell can be run like this 
$ octopuslb-admin -e "show"
OR
$ octopuslb-admin -f /var/run/octopuslb/octopus-1.0-0.0.0.0-1080 -e "algorithm HASH"
if you were running multiple instances 

	
Q. What do the different levels of debugging show?
A.
 1 = server errors, init msgs
 2 = new connections. Monitor process logs
 3 = session maintenance. Traffic
 4 = Session EOF/Error messages
 5 = socket state dump
