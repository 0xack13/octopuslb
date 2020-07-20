#!/usr/bin/ruby

#placeholder for test script
#assumes a clean build and that no instances of octopus are running on the server

ADMIN ="../octopuslb-admin"
SERVER ="../octopuslb-server"
CONF_FILE ="./sample.conf"

def error(text)
	puts "ERROR DETECTED!\n"
	puts text
	exit(1)
end

#clean slate: kill the server, remove all logs and run files
def cleanSlate
	ret=`killall -9 octopuslb-server`
	ret=`rm -f /var/log/octopuslb/*`
	ret=`rm -f /var/run/octopuslb/*`
end

def startServer
	puts "Starting server"
	ret=system("#{SERVER} -c #{CONF_FILE}")
	if $? != 0
		error("")
	end
end

def startServerWithDebug(level)
	puts "Starting server with debug level #{level}"
	ret=system("#{SERVER} -c #{CONF_FILE} -d #{level}")
	if $? != 0
		error("")
	end
end
def startServerExpectFail
	ret=`#{SERVER} -c #{CONF_FILE}`
	if $? == 0
		error("")
	end
	return ret;
end

def killServer
	puts "Killing Server"
	ret=system("#{ADMIN} -e \"kill\"")
	if $? != 0
		error("")
	end
end

def runABSingleThread(count)
	ret=`ab -q -c 1 -n #{count} http://127.0.0.1:1080/`
	if $? != 0
		error(ret)
	end
end

def runABSpam(concurrency,count)
	ret=`ab -q -c #{concurrency} -n #{count} http://127.0.0.1:1080/`
	if $? != 0
		error(ret)
	end
end

def verifyConnectionCount(count)
	line_counter=0
	connection_count=0
	ret=`#{ADMIN} -m -e "show"`
	if $? != 0
		error(ret)
	end

	#remove first line
	ret.each do |line|
		line_counter+=1
		if line_counter == 1
			next
		end
		input=line.split(',')
		if input[3] == "Deleted"
			print "	#{input[2].strip}: 0!"
		else
			connection_count += input[9].to_i
			print "	#{input[2].strip}:#{input[9].strip} "
		end

	end
	print "	Total: #{connection_count}\n"
	if count != -1
		if connection_count != count
			error("Connection count did not match up\n")
		end
	end
end

def runAdminCommand(cmd)
	ret=`#{ADMIN} -e "#{cmd}" 2>&1`
	if $? != 0
		error(ret)
	end
	return ret
end

def runAdminCommandExpectFail(cmd)
	ret=`#{ADMIN} -e "#{cmd}"`
	if $? == 0
		error(ret)
	end
	return ret
end

def runTest100
	runAdminCommand("reset all")
	runABSingleThread(100)
	verifyConnectionCount(100)
end

def runTestSpam
	runAdminCommand("reset all")
	runABSpam(100,1000)
	verifyConnectionCount(-1)
end

def testAlgorithmChanges
	runAdminCommand("algorithm rr")
	print "RR algorithm		"
	runTest100
	runAdminCommand("algorithm ll")
	print "LL algorithm		"
	runTest100
	runAdminCommand("algorithm lc")
	print "LC algorithm		"
	runTest100
	runAdminCommand("algorithm hash")
	print "HASH algorithm		"
	runTest100
	runAdminCommand("algorithm static")
	print "STATIC algorithm	"
	runTest100
end

def testCreateMember
	puts "Creating new member server"
	runAdminCommand("create member local3 127.0.0.1 80")
	runAdminCommand("enable all")
	testAlgorithmChanges
end

def testDisableEnable1stMember
	runAdminCommand("disable member 0")
	puts "Disabling member #0"
	testAlgorithmChanges
	puts ""
	runAdminCommand("enable member 0")
	puts "Enabling member #0"
	testAlgorithmChanges
end

def testDelete1stMember
	puts "Deleting member #0"
	runAdminCommand("delete member 0")
	testAlgorithmChanges
end

def testAttemptToStartTwice
	puts "Attempt to start server twice: "
	startServerExpectFail
end

def testStandby
	runAdminCommand("algorithm lc")
	puts "Limiting connections to member #0 and setting member #1 to standby"
	runAdminCommand("maxc member 0 1")
	runAdminCommand("standby member 1")
	runTestSpam
	puts "Removing connection limits for member #0"
	runAdminCommand("maxc member 0 500")
	runTestSpam
	puts "Setting member #1 to non-standby"
	runAdminCommand("standby member 1")
	runTestSpam

end

def testCloning
	puts "Creating new clone #0"
	runAdminCommand("algorithm rr")
	runAdminCommand("create clone clone1 127.0.0.1 80")
	runAdminCommand("enable clone 0")
	runTest100
	puts "Enabling clone mode"
	runAdminCommand("clone enable")
	runTestSpam
	puts "Creating new clone #1"
	runAdminCommand("create clone clone2 127.0.0.1 80")
	runAdminCommand("enable clone 1")
	runTestSpam
	puts "Disabling clone #0"
	runAdminCommand("disable clone 0")
	runTestSpam
	puts "Disabling clone #1"
	runAdminCommand("disable clone 1")
	runTest100
	puts "Deleting clones"
	runAdminCommand("delete clone 0")
	runAdminCommand("delete clone 1")
	runTest100
end

def testAdminCommands
	#test all valid commands and make sure successful return
end

def testConfigFileParameters
	#try all sorts of permutations of values (corrent and incorrect) in the config file
end

cleanSlate

print "Attempting to start administration interface before server has started: "
print "#{runAdminCommandExpectFail("show")}"
puts ""

startServer

puts ""
puts "Test Algorithms"
testAlgorithmChanges

puts ""
puts "Creating new member server"


testDisableEnable1stMember
puts ""

testAttemptToStartTwice
puts ""

testStandby
puts ""

testCreateMember
puts ""

testDelete1stMember
puts ""

testCloning
puts ""

killServer
puts ""

startServerWithDebug(5)
puts ""

killServer
puts ""
#puts "#{runAdminCommand("show")}"
puts ""
puts "SUCCESS! All tests passed"
puts ""
exit
