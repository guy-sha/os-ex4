

#include <iostream>
#include "../../malloc_1.cpp"

#define NUM_TESTS 1
#define MAX_SIZE 100000000

#define ASSERT_TRUE(expr)  		\
do {			 	 \
	if((expr) == false) {	  \
		std::cout << "Assertion Failed in " << __FILE__ << ":" << __LINE__  << std:: endl;	\
		return false;       \
	}			     \
} while(0)


#define RUN_TEST(indx) 				       \
do {							\
	std::cout << "running test" << indx << "...  ";  \
	if(tests[indx-1]()) {				  \
		std::cout << "[OK]" << std::endl;	   \
	} else {					    \
		std::cout << "[FAILED]" << std::endl; 	     \
	}						      \
} while(0)

static long addToLong(void* ptr) {
	long n = (long)(ptr);
	return n;
}

bool test1(void) {
	long base = addToLong(sbrk(0));
	void* p1 = smalloc(MAX_SIZE+1);
	long tmp1 = addToLong(sbrk(0));
	ASSERT_TRUE((tmp1-base) == 0);
	void* p2 = smalloc(MAX_SIZE);
	long d1 = addToLong(sbrk(0));
	ASSERT_TRUE((d1-base) == MAX_SIZE);
	void* p3 = smalloc(15);
	long d2 = addToLong(sbrk(0));
	ASSERT_TRUE((d2-d1) == 15);
	void* p4 = smalloc(0);
	long d3 = addToLong(sbrk(0));
	ASSERT_TRUE((d3-d2) == 0);
	return true;
}


bool (*tests[NUM_TESTS])(void) = {
	test1
};

int main(int argc, char* argv[]) {
	if(argc != 1) {
		std::cerr << "Invalid Usage" << std::endl;
		return 1;
	}
	std::cout << "Hussam's tests:" << std::endl;
	for(unsigned i = 0; i < NUM_TESTS; i++) {
		RUN_TEST(i+1);
	}
	return 0;
}