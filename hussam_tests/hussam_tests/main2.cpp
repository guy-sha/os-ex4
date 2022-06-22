
#include <iostream>
#include "../../malloc_2.cpp"


/*
 * main2.cpp is used to test your malloc_2.cpp file.
 * testing is done by redirecting output (in testing scripting) to the output/expected folder,
 * and comparing files (in bash).
 */


#define NUM_TESTS 4
#define MAX_SIZE 100000000

#define RUN_TEST(indx) 		\
	do {			 \
		tests[indx]();	  \
	} while(0)


void print(void) {
	unsigned num_free_blocks = _num_free_blocks();
	unsigned num_free_bytes = _num_free_bytes();
	unsigned num_allocated_blocks =_num_allocated_blocks();
	unsigned num_allocated_bytes =_num_allocated_bytes();
	unsigned num_beta_bytes =_num_meta_data_bytes();
	unsigned size_meta_data =_size_meta_data();
	std::cout << "num_free_blocks = " << num_free_blocks << std::endl;
	std::cout << "num_free_bytes = " <<  num_free_bytes << std::endl;
	std::cout << "num_allocated_blocks = " << num_allocated_blocks  << std::endl; 
	std::cout << "num_allocated_bytes = " <<  num_allocated_bytes << std::endl; 
	std::cout << "num_beta_bytes = " <<  num_beta_bytes << std::endl; 
	std::cout << "size_meta_data = " <<  size_meta_data << std::endl;
	std::cout << std::endl;  
}


void test1() {
	void* p1 = smalloc(10);
	void* p2 = smalloc(20);
	void* p3 = smalloc(30);
	void* p4 = smalloc(40);
	void* p5;
	sfree(p5);
	print();
	sfree(p4);
	sfree(p3);
	sfree(p2);
	sfree(p1);
	print();
}


void test2() {
	void* p1 = smalloc(10);
	print();
	sfree(p1);
	print();
	p1 = smalloc(5);
	print();
	sfree(p1);
	print();
	p1 = smalloc(10);
	sfree(p1);
	print();
	p1 = smalloc(11);
	print();
	sfree(p1);
	print();
}


void test3() {
	void* p1 = smalloc(100);
	void* p2 = p1;
	p1 = smalloc(MAX_SIZE+1);
	if(p1 == nullptr) {
		std::cout << "p1 is nullptr" << std::endl;
	} else {
		std::cout << "p1 is not nullptr" << std::endl;
	}
	p1 = p2;
	print();
	p2 = smalloc(50);
	p1 = srealloc(p1,300);
	void* p3 = smalloc(25);
	print();
	sfree(p3);
	void* p4 = scalloc(10,4);
	for(unsigned i = 0; i < 40; i++) {
		char* tmp = (char*)(p4)+i;
		if(*tmp != 0) {
			std::cerr << "should be zero" << std::endl;
		}
	}
	sfree(p1);
	sfree(p2);
	sfree(p4);
	print();
}


void test4() {
	void* p1 = srealloc(nullptr,10);
	print();
	p1 = srealloc(nullptr,20);
	print();
	p1 = srealloc(nullptr,30);
	print();
	p1 = srealloc(nullptr,40);
	print();
	sfree(p1);
	print();
	p1 = scalloc(5,2);
	sfree(p1);
	print();
	p1 = scalloc(5,8);
	sfree(p1);
	print();
	p1 = scalloc(5,12);
	print();
	sfree(p1);
}


void (*tests[NUM_TESTS])(void) = {
	test1,
	test2,
	test3,
	test4
};
	

int main(int argc,char* argv[]) {
	if(argc != 2) {
		std::cerr << "Invalid Usage - Use: <prog_name> <index>" << std::endl;
		return 1;
	}
	unsigned indx = strtol(argv[1],nullptr,10);
	if(indx >= NUM_TESTS) {
		std::cerr << "Invalid Test Index - Index: {0.." << NUM_TESTS - 1 << "}" << std::endl;
		return 1;
	}
	RUN_TEST(indx);
	return 0;
}