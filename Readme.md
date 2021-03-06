Crossbow
========
Crossbow is a collection of C++ library used in the Tell system. It contains
several libraries, some of them are header only.

Install instructions
--------------------
Crossbow uses CMake and requires a C++ compiler that supports C++11. If nothing
else is written here, the libraries are tested on Linux with gcc and Mac OS X
with clang and libc++.

To use the header only libraries, you can just unpack/clone the crossbow source
directory and add the directory to your include path.

To install the crossbow libraries you should create a build directory and execute
cmake followed by make install (or ninja install). You can specify an install prefix
to change the install location.

**Example:** (to install to /opt/local)
```bash
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/opt/local ..
make
make install
```

If a library has some additional depenedencies, cmake with only build and install them
if it can find the dependencies installed on your system. Please look into the sections
for each library to find out, what kind of dependencies the library has (if any).

string (header only)
--------------------
crossbow::string is a replacememnt for std::string. crossbow::string allocates a
32 byte buffer on the stack when it is constructed. It can store up to 30 (for
char strings) or 15 (for wchar strings) characters on the stack space. If the string
grows bigger than that, it will allocate space on the heap like std::string and use
the 32 byte buffer to safe the pointer, size, and capacity of the string.
If you have a lot of small strings in your system, this string type might give you
additional performance, since it might prevent a lot of calls to malloc/free.

**Dependencies**: This library does not have any dependencies.

singleton (header only)
-----------------------
crossbow::singleton is a thread safe, easy to use implementation of the singleton
pattern. It is highly configurable and easy to use. The easiest way to use the
singleton class is as follows:

```c++
class mysingleton_impl
{
    friend class crossbow::singleton<mysingleton_impl>;
    private:
        mysingleton_impl() {}
    public:
        void foo() { /* do something meaningful */ }
};

using mysingleton = crossbow::singleton<mysingleton_impl>;

int main()
{
    mysingleton inst; // get the singleton
    inst->foo(); // call foo on it
}
```

The singleton class can be configured with several parameters. These are:

__**Create policy**__
The default create policy is create_static. This will use a statically allocated block
to store the singleton. Others are:
-  *create_using_new*: Will use new and delete to allocate the memory for the singleton object.
-  *create_using_malloc*: Uses malloc/free
-  *create_using*: This takes another teamplate argument which has to be an allocator. This allocator will be used to create and delete the object.
-  You can also create your own lifetime policy.

__**Lifetime Policy**__
The lifetime policy controls how long the object lives. Options are:
- *default_lifetime*: Will destroy the object as soon as the program quits.
- *phoenix_lifetime*: Same as default_lifetime, but this one will recreate
the object if it gets referenced again. This might be useful, if you have
singletons referencing other singletons. But be careful: if your singleton
uses other resources (like files), this might not be unproblematic.
- *infinite_lifetime*: As it says: the singleton object will never be destroyed.
Leak detection tools like valgrind might complain about a memory leak if
you use this lifetime policy. Usually this can be ignored, since a singleton
lives until the end of program execution anyway. But be carefull with file
descriptors etc.

__**Mutex**__
The singleton class is thread safe and uses a mutex to assure thread safety.
The Mutex class has to implement the lock/unlock methods from std::mutex
(which is also the default mutex the singleton uses). CAUTION: thread safe
only means, that the construction/destruction of your object is thread safe,
not access to your object itself.
If you use singleton in a single-threaded environment, you can use
`crossbow::no_lock` here. In that case, the singleton won't use any locking at all.

**Dependencies**: This library does not have any dependencies.

singleconsumerqueue (header only)
---------------------------------
This is a lock-free queue, which works for one consumer and multiple producers.
The main reason to chose this queue over others like ##boost::lockfree::queue##
is the support for multipull - so the consumer can pull multiple elements out of
the queue at once.

**Dependencies**: This library does not have any dependencies.

concurrent_map (header only)
----------------------------
This is an implementation of a thread safe hash map. It does not support iteration,
nor does it ever return references to the values hold in the queue, since a resize
might happen at any point in time. But it has support for ##for_each##, which will
execute a function on all elements in the queue. Furthermore ##exec_on## can be used
to call a function on one element of the queue. ##exec_on## can even be used for
insertion. The function passed to ##exec_on## is required to return a boolean. If
it returns false, the value will be left in the map, otherwise it will get deleted.

**Dependencies**: This library does not have any dependencies.

program_options (header only)
-----------------------------
This is a small replacement for boost::program options. The reason to have an alternative
to boost::program_options is mostly because using boost will introduce a link time
dependency, which crossbow::program_options is header only. A small example on how  to
use program_options can be found in the test folder.

allocator library
-----------------
This library contains two special allocators. The first, called ChunkAllocator, allocates
memory in chunks. This means, that when instanciated, it will allocate a big (usually
about 1 MB) chunk of memory. Whenever memory from the pool is request, it will just
increase a pointer and return a pointer to its allocated chunk. Whenever the chunk is full,
the pool will allocate a new chunk. On deallocation, the memory won't be freed. As soon as
the pool gets destroyed, all its memory is free at once. This makes allocation and
deallocation very cheap whenever a set of object shares the same life time.

The other allocator implements the epoch algorithm and is used for the implementation
of lock-free data structures.

