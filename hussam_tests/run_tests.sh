#! /bin/bash

NC='\033[0m'
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;36m'
PURPLE='\033[0;35m'

g++ -std=c++11 ./hussam_tests/main2.cpp -o test_prog
i=0
NUM_TESTS=4
TEST2_PATH="./hussam_tests/tests_malloc2"

echo -e "${PURPLE}Hussam's tests:${NC}"

while [[ i -lt NUM_TESTS ]]
do
	j=i
	((j++))
	echo -e -n "${BLUE}running test ${j}...  ${NC}"
	#valgrind --leak-check=full --error-exitcode=11 --log-file="${TEST2_PATH}/test${j}.valgrind" \
	#	./test_prog ${i} > ${TEST2_PATH}/test${j}.expected
	#valgrind_out=$?
	./test_prog ${i} > ${TEST2_PATH}/test${j}.yourout
	cmp -s "${TEST2_PATH}/test${j}.expected" "${TEST2_PATH}/test${j}.yourout"
	res=$?	#if files are equal, then res should be 0.
	#echo "res = ${res}"
	if [[ $res -eq 0 ]]
	then
		echo -e "${GREEN}[OK]${NC}"
	else
		echo -e "${RED}[FAIL]${NC}"
	fi
	((i++))
done

rm ./test_prog
