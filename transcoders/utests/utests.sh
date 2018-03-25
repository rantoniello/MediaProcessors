#!/bin/bash

# Arguments description:
# $1: project installation directory path
# $2 program to execute
# $3 use valgrind [yes/no]

VALGRIND_ERROR_CODE=254
GENERIC_ERROR_CODE=255
TESTDIR=tests
USE_VALGRIND=$3

VALGRIND_OPTIONS="--error-exitcode=$VALGRIND_ERROR_CODE --leak-check=full"
#--show-reachable=yes --track-origins=yes"
#--log-file="my log file full path and name"
#-v --track-origins=yes

if [ "$USE_VALGRIND" == 'yes' ] ; then
	LD_LIBRARY_PATH=$1/lib stdbuf -o0 -e0 $1/bin/valgrind $VALGRIND_OPTIONS \
	$1/bin/$2
else
	LD_LIBRARY_PATH=$1/lib $1/bin/$2
fi

TEST_STATUS=$?
#echo $TEST_STATUS # debugging purposes

echo ""
if [ $TEST_STATUS -eq $VALGRIND_ERROR_CODE ] ; then
	echo "Valgrind FAILED, check for memory leaks"
	exit $VALGRIND_ERROR_CODE
elif [ $TEST_STATUS -ne 0 ] ; then
	# The program '$2' will be in charge of specifying the error in a message
	exit $GENERIC_ERROR_CODE
fi

echo "All tests passed successfully"
echo""

exit 0
