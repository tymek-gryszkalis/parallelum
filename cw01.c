#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>        
#include <fcntl.h>           
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define SHM_NAME "/calc"
#define SHM_NAME_RES "/res"
#define BUFFOR_SIZE 80

void on_usr1(int signal)
{
    printf("Otrzymałem USR1\n");

}

void on_usr2(int signal, siginfo_t *siginfo, void *context)
{
    printf("Otrzymałem USR2 od %d\n", siginfo->si_pid);
}


int read_vector_from_file(const char* filename, double** vector)
{
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFOR_SIZE + 1];
    int n;
    if (fgets(buffer, BUFFOR_SIZE, f) == NULL) {
        fclose(f);
        return -1; // Error or end of file
    }
    n = atoi(buffer);

    vector = (double)malloc(sizeof(double) * n);
    if (*vector == NULL) {
        perror("malloc");
        fclose(f);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n; i++) {
        if (fgets(buffer, BUFFOR_SIZE, f) == NULL) {
            break; // Error or end of file
        }
        (*vector)[i] = atof(buffer);
    }
    fclose(f);

    return n; // Return the number of elements in the vector
}

double* initialize_shared_memory(size_t size, int type)
{
    char *name = (type==1)?SHM_NAME:SHM_NAME_RES;
    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(shm_fd, size * sizeof(double)) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }

    double* vector = mmap(NULL, size * sizeof(double), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (vector == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    return vector;
}

void create_children(int n, int vector_size, double *shared_vector, double *results)
{
    for (int i = 0; i < n; i++)
    {
        if (fork() == 0)
        {
            int id = i;

            printf("[son] pid %d from [parent] pid %d\n",getpid(),getppid());

            sigset_t mask;
            struct sigaction usr1;
            sigemptyset(&mask);
            usr1.sa_handler = (&on_usr1);
            usr1.sa_mask = mask;
            usr1.sa_flags = SA_SIGINFO;
            sigaction(SIGUSR1, &usr1, NULL);

            sigemptyset(&mask);
            sigaddset(&mask, SIGTSTP);
            sigprocmask(SIG_BLOCK, &mask, NULL);


            kill(getppid(), SIGUSR2);

            pause();

            int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
            if (shm_fd == -1) {
                perror("Child shm_open");
                exit(EXIT_FAILURE);
            }

            shared_vector = mmap(NULL, vector_size * sizeof(double), PROT_READ, MAP_SHARED, shm_fd, 0);
            if (shared_vector == MAP_FAILED) {
                perror("Child mmap");
                exit(EXIT_FAILURE);
            }

            int shm_fd2 = shm_open(SHM_NAME_RES, O_RDONLY, 0666);
            if (shm_fd2 == -1) {
                perror("Child shm_open");
                exit(EXIT_FAILURE);
            }

            results = mmap(NULL, n * sizeof(double), PROT_READ, MAP_SHARED, shm_fd2, 0);
            if (results == MAP_FAILED) {
                perror("Child mmap");
                exit(EXIT_FAILURE);
            }

            int ile = id<=vector_size%n? ((vector_size/n)+1):(vector_size/n);

            
            

            exit(0);
        } 
    }
}

int main(int argc, char **argv)
{
    pid_t my_pid = getpid();
    int n = 5;
    printf("parent \n");

    struct sigaction usr2;
    sigemptyset(&usr2.sa_mask);
    usr2.sa_sigaction = &on_usr2; 
    usr2.sa_flags = SA_SIGINFO; 
    sigaction(SIGUSR2, &usr2, NULL);

    double *local_vector;
    int vector_size = read_vector_from_file("vector.dat", &local_vector);
    if (vector_size < 0) {
        fprintf(stderr, "Failed to read vector from file\n");
        exit(EXIT_FAILURE);
    }

    double *shared_vector = initialize_shared_memory(vector_size, 1);

    double *results = initialize_shared_memory(n, 2);

    for (int i = 0; i < vector_size; i++) {
        shared_vector[i] = local_vector[i];
    }
    free(local_vector); 

    create_children(n, vector_size, shared_vector, results);

    while (1)
        pause(); 

    return 0;
}