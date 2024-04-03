/*
 * Swarthmore College, CS 31
 * Copyright (c) 2021 Swarthmore College Computer Science Department,
 * Swarthmore PA
 */

// Erebus Oh, Jacinta Fernandes-Brough
// CPSC 31, Lab 9

#include <pthreadGridVisi.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include "colors.h"

/****************** Definitions **********************/
/* Three possible modes in which the GOL simulation can run */
#define OUTPUT_NONE   0   // with no animation
#define OUTPUT_ASCII  1   // with ascii animation
#define OUTPUT_VISI   2   // with ParaVis animation

/* Used to slow down animation run modes: usleep(SLEEP_USECS);
 * Change this value to make the animation run faster or slower
 */
//#define SLEEP_USECS  1000000
#define SLEEP_USECS    100000

/* A global variable to keep track of the number of live cells in the
 * world (this is the ONLY global variable you may use in your program)
 */
static int total_live = 0;

// mutex
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// barrier
static pthread_barrier_t barrier;

/* This struct represents all the data you need to keep track of your GOL
 * simulation.  Rather than passing individual arguments into each function,
 * we'll pass in everything in just one of these structs.
 * this is passed to play_gol, the main gol playing loop
 *
 * NOTE: You will need to use the provided fields here, but you'll also
 *       need to add additional fields. (note the nice field comments!)
 * NOTE: DO NOT CHANGE THE NAME OF THIS STRUCT!!!!
 */
struct gol_data {

  // NOTE: DO NOT CHANGE the names of these 4 fields (but USE them)
  int rows;  // the row dimension
  int cols;  // the column dimension
  int iters; // number of iterations to run the gol simulation
  int output_mode; // set to:  OUTPUT_NONE, OUTPUT_ASCII, or OUTPUT_VISI
  int numThreads; // number of threads to use
  int partitionCol; // 0 = row partition threads, 1 = col partition threads
  int printThreads; // 0 = no printing, 1 = print thread allocation info
  int tid; // logical thread id
  int rowStart; // row and column thread partitioning information
  int rowStop;
  int colStart;
  int colStop;


  int *board;
  int *boardCopy;
   /* fields used by ParaVis library (when run in OUTPUT_VISI mode). */
   // NOTE: DO NOT CHANGE their definitions BUT USE these fields
   visi_handle handle;
   color3 *image_buff;
};

/****************** Function Prototypes **********************/
/* the main gol game playing loop (prototype must match this) */
void *play_gol(void *args);
/* init gol data from the input file and run mode cmdline args */
int init_game_data_from_args(struct gol_data *data, char *argv[]);

/* print board to the terminal (for OUTPUT_ASCII mode) */
void print_board(struct gol_data *data, int round);
// Given gol_data, and coordinates, returns number of live
// neighbors coordinate has
int numNeighbors(int* b, int x, int y, int rows, int cols);
// calculate neighbor coordinates with torus
int getTorusCoords(int x, int max);
// updates total_live global variable
int recalculateNumAlive(struct gol_data *data);
// update colors for ParVisi to show board
void update_colors(struct gol_data *data);
// function to read 1 int into variable
void readOneInt(FILE *infile, int *a);
// sets up board on init
void initializeBoards(struct gol_data *data, FILE *infile);
// handles command line argument errors
void checkInput(int input, int min, int max, char error[]);
// partition board to threads
void partition(int tid, struct gol_data *data,
  int *startRow, int *stopRow, int *startCol, int *stopCol);
// copy gol data for worker threads
void initGolData(struct gol_data *data, struct gol_data *dest,
  int tid, int rowStart, int rowStop, int colStart, int colStop);
// print thread partitioning information
void printThreadPartitionInfo(struct gol_data *data);
/************ Definitions for using ParVisi library ***********/
/* register animation with Paravisi library (DO NOT MODIFY) */
int connect_animation(void (*applfunc)(struct gol_data *data),
    struct gol_data* data);
/* name for visi (you may change the string value if you'd like) */
static char visi_name[] = "GOL!";

/**********************************************************/
int main(int argc, char *argv[]) {

  int ret, i, n, a, b, c, d;
  struct gol_data data, *threadData;
  double secs, startT, stopT;
  struct timeval start_time, stop_time;
  //pthread_t *threadArray;
  int *threadIDs;
  pthread_t *threadArray;

  /* check number of command line arguments */
  if (argc < 3) {
    printf("usage: ./gol <infile> <0|1|2>\n");
    printf("(0: with no visi, 1: with ascii visi, 2: with ParaVis visi)\n");
    exit(1);
  }

  /* Initialize game state (all fields in data) from information
   * read from input file */
  ret = init_game_data_from_args(&data, argv);
  if(ret != 0) {
    printf("Error init'ing with file %s, mode %s\n", argv[1], argv[2]);
    exit(1);
  }

  // init threads
  n = data.numThreads;
  threadArray = malloc(n*sizeof(pthread_t));
  threadIDs = malloc(n*sizeof(int));
  threadData = malloc(n*sizeof(struct gol_data));
  // init pthread_barrier_t
  if(pthread_barrier_init(&barrier, 0, n)) {
    printf("pthread_barrier_init error\n");
    exit(1);
  }

  if(data.output_mode == OUTPUT_VISI){
    data.handle = init_pthread_animation(n, data.rows, data.cols, visi_name);
    if(data.handle == NULL) {
      printf("ERROR init_pthread_animation\n");
      exit(1);
    }
    // get the animation buffer
    data.image_buff = get_animation_buffer(data.handle);
    if(data.image_buff == NULL) {
      printf("ERROR get_animation_buffer returned NULL\n");
      exit(1);
    }
  }
  ret = gettimeofday(&start_time, NULL);
  for(i = 0; i < n; i++){
    // make logical thread id
    threadIDs[i] = i;
    // calculate partition for thread
    partition(threadIDs[i], &data, &a, &b, &c, &d);
    // create and copy gol_data
    initGolData(&data, &threadData[i], threadIDs[i], a, b, c, d);
    // create thread
    pthread_create(&threadArray[i], NULL, play_gol, &threadData[i]);
  }

  if(data.output_mode == OUTPUT_VISI){
    run_animation(data.handle, data.iters);
  }

  // join all threads before exiting
  for (i = 0; i < n; i++) {
    pthread_join(threadArray[i], NULL);
  }

  /* Invok8,612 bytes in 6 blockse play_gol in different ways based on the run mode */
  if(data.output_mode == OUTPUT_NONE) {  // run with no animation

  }else if (data.output_mode == OUTPUT_ASCII) { // run with ascii animation

    // clear the previous print_board output from the terminal:
    // (NOTE: you can comment out this line while debugging)
    //if(system("clear")) { perror("clear"); exit(1); }

    // NOTE: DO NOT modify this call to print_board at the end
    //       (its for grading)
    print_board(&data, data.iters);
  }

  ret = gettimeofday(&stop_time, NULL);

  if (data.output_mode != OUTPUT_VISI) {
    startT = (start_time.tv_sec + (start_time.tv_usec / 1000000.0));
    stopT = (stop_time.tv_sec + (stop_time.tv_usec / 1000000.0));
    //printf("%f %f \n", startT, stopT);//
    secs = stopT - startT;

    /* Print the total runtime, in seconds. */
    // NOTE: do not modify these calls to fprintf
    fprintf(stdout, "Total time: %0.3f seconds\n", secs);
    fprintf(stdout, "Number of live cells after %d rounds: %d\n\n",
        data.iters, total_live);
  }

  // free board
  free(data.board);
  //free board copy
  free(data.boardCopy);
  // free thread
  free(threadArray);
  // free thread IDs
  free(threadIDs);
  // free thread gol_data
  free(threadData);
  return 0;
}
/**********************************************************/
/* Initialize gol_data struct for threads
*  Params: original data, thread data to init, thread id,
*  and partitioning information.
*
*  No return, porperly initializes destination struct
*/
void initGolData(struct gol_data *data, struct gol_data *dest,
  int tid, int rowStart, int rowStop, int colStart, int colStop){
  dest->rows = data->rows;
  dest->cols = data->cols;
  dest->iters = data->iters;
  dest->output_mode = data->output_mode;
  dest->numThreads = data->numThreads;
  dest->partitionCol = data->partitionCol;
  dest->printThreads = data->printThreads;
  dest->board = data->board;
  dest->boardCopy = data->boardCopy;
  dest->handle = data->handle;
  dest->image_buff = data->image_buff;

  dest->tid = tid;
  dest->rowStart = rowStart;
  dest->rowStop = rowStop;
  dest->colStart = colStart;
  dest->colStop = colStop;
}
/**********************************************************/
/* Prints Thread Partition information
*  Given thread's gol data, print partition info
*/
void printThreadPartitionInfo(struct gol_data *data){
  int totalCol, totalRow, a, b, c, d;
  a = data->rowStart;
  b = data->rowStop;
  c = data->colStart;
  d = data->colStop;
  totalCol = d - c + 1;
  totalRow = b - a + 1;

  printf("tid %3d: rows: %3d:%3d (%d) cols: %3d:%2d (%d)\n",
    data->tid,a,b,totalRow,c,d,totalCol);
  fflush(stdout);
}
/**********************************************************/
/* Partition function
*  Params: thread ID, original gol data, and pointers to where
*  partition info should be returned to
*
*  Updates partition variables to the proper coordinates
*/
void partition(int tid, struct gol_data *data, int *startRow,
  int *stopRow, int *startCol, int *stopCol){
  int i, partitionSize, evenSize, n, remainder, offset;
  int *numerator, *other, *constantStart, *constantStop, *start, *stop;
  n = data->numThreads;

  if(data->partitionCol){
    numerator = &data->cols;
    other = &data->rows;
    constantStart = startRow;
    constantStop = stopRow;
    start = startCol;
    stop = stopCol;
  }else{
    numerator = &data->rows;
    other = &data->cols;
    constantStart = startCol;
    constantStop = stopCol;
    start = startRow;
    stop = stopRow;
  }

  evenSize = *numerator/n;
  partitionSize = evenSize;
  remainder = *numerator%n;
  if(tid < remainder){
    partitionSize++;
  }
  offset = 0;
  for(i = 0; i < remainder; i++){
    offset++;
  }

  *constantStart = 0;
  *constantStop = *other - 1;
  *start = (tid*evenSize) + offset;
  *stop = *start + partitionSize - 1;

}
/**********************************************************/
/* Given a file and gol_data, initializes gol_data's board
 *
 * Helper function for init game.
 */
void initializeBoards(struct gol_data *data, FILE *infile){
  int i, j, ret, r, c, numAlive, a, b;

  readOneInt(infile, &numAlive);
  r = data->rows;
  c = data->cols;

  //initialize board to all 0s
  data->board = malloc(r*c*sizeof(int));
  data->boardCopy = malloc(r*c*sizeof(int));

  for(i = 0; i < r; i++){
    for(j = 0; j < c; j++){
      data->board[i*c+j] = 0;
      //data->boardCopy[i*c+j] = 0;
    }
  }
  ret = 1;
  //read in alive people coordinates
  if(ret == 1){
    for(i = 0; i < numAlive; i++){
      ret = fscanf(infile, "%d%d", &a, &b);
      data->board[a*c+b] = 1;
    }
  }else{
    printf("Error: reading in live cell coordinates.\n");
    exit(1);
  }
}
/**********************************************************/
/* Given a file and variable address, reads 1 integer and
 * puts it in the variable.
 *
 * Helper function for init game.
 */
void readOneInt(FILE *infile, int *a){
  int ret;
  ret = fscanf(infile, "%d", a);
  if(ret != 1){
    printf("Error: Reading in values.");
    exit(1);
  }
}
/**********************************************************/
/* Input Checker
*  Helper Function for init game from args functions
*  Params: input, min, max, error message
*
*  Function exits the program if input is outside of bounds
*  with error message.
*/
void checkInput(int input, int min, int max, char error[]){
  if((input < min) || (input > max)){
    printf("Error: %s.\n", error);
    exit(1);
  }
}
/**********************************************************/
/* Given gol_data and a coordinate, update total_live
 *
 * Helper function for init game from args
 */
int recalculateNumAlive(struct gol_data *data){
  int total, i, j;
  total = 0;

  for(i=0; i < data->rows; i++){
    for(j=0; j < data->cols; j++){
      if(data->board[i*data->cols+j] == 1){
        total++;
      }
    }
  }
  return total;
}
/**********************************************************/
/* initialize the gol game state from command line arguments
 *       argv[1]: name of file to read game config state from
 *       argv[2]: run mode value
 * data: pointer to gol_data struct to initialize
 * argv: command line args
 *       argv[1]: name of file to read game config state from
 *       argv[2]: run mode
 *       argv[3]: number of threads to use
 *       argv[4]: partition as col? 0 = row, 1 = col
 *       argv[5]: print thread allocation information?
 * returns: 0 on success, 1 on error
 */
int init_game_data_from_args(struct gol_data *data, char *argv[]) {

  data->output_mode = atoi(argv[2]);
  checkInput(data->output_mode, 0, 2, "Invalid output mode");
  data->numThreads = atoi(argv[3]);
  checkInput(data->numThreads, 0, 9999999, "Invalid number of threads");
  data->partitionCol = atoi(argv[4]);
  checkInput(data->partitionCol, 0, 1, "Invalid thread partitioning mode");
  data->printThreads = atoi(argv[5]);
  checkInput(data->printThreads, 0, 1, "Invalid thread printing mode");


  FILE *infile;
  int r, c, iter;

  //check that we can open file properly
  infile = fopen(argv[1], "r");
  if (infile == NULL){
    printf("Error: file open %s\n", argv[1]);
    exit(1);
  }
  //read in row, col, number of iterations, and number of coordinate
  readOneInt(infile, &r);
  data->rows = r;
  readOneInt(infile, &c);
  data->cols = c;
  readOneInt(infile, &iter);
  data->iters = iter;

  // initialize board and board copy
  initializeBoards(data, infile);

  total_live = recalculateNumAlive(data);

  fclose(infile);

  // start ParaVisi animation
  if (data->output_mode == 2){
    run_animation(data->handle, data->iters);
  }

  // make sure number of threads is within max
  if(data->partitionCol){ // col partitioning
    checkInput(data->numThreads, 0, c, "Number of threads too big");
  }else{ // row partitioning
    checkInput(data->numThreads, 0, r, "Number of threads too big");
  }

  return 0;
}
/**********************************************************/
/* Given a coordinate and maximum, return the proper torus
 * coordinate as if was linked 1D list with lenth of max
 *
 * Helper function for numNeighbors.
 */
int getTorusCoords(int coord, int max){
  if(coord<0){
    return (coord%max)+max;
  }else if(coord >= max){
    return coord%max;
  }else{
    return coord;
  }
}
/**********************************************************/
/* Given gol_data and a coordinate, return the number of
 * live neighbors
 *
 * Helper function for play_gol
 */
int numNeighbors(int* b, int x, int y, int rows, int cols){
  int aroundNums[3], i, j, tempX, tempY, total;
  aroundNums[0] = -1;
  aroundNums[1] = 0;
  aroundNums[2] = 1;
  total = 0;

  for(i = 0; i < 3; i++){
    for(j = 0; j < 3; j++){
      if(aroundNums[i] == 0 && aroundNums[j] == 0){
        continue;
      }else{
        tempX = getTorusCoords(x + aroundNums[i], rows);
        tempY = getTorusCoords(y + aroundNums[j], cols);
        //printf("Looking at (%d,%d)\n", tempX, tempY);
        if(b[tempX*rows+tempY] == 1){
          total++;
        }
      }
    }
  }
  //printf("%d", total);
  return total;
}
/**********************************************************/
/* updates the colors (for each thread)*/
void update_colors(struct gol_data *data) {
  int i, j, r, c, buff_i, index;
  color3 *buff;

  buff = data->image_buff;
  r = data->rows;
  c = data->cols;

  for(i = data->rowStart; i <= data->rowStop; i++) {
    for (j = data->colStart; j <= data->colStop; j++) {
      index = i*c+j;
      buff_i = (r - (i+1))*c + j;
      if (data->board[index] == 1) {
        buff[buff_i].r = 179;
        buff[buff_i].g = 102;
        buff[buff_i].b = 255;
        // buff[buff_i] = c3_black;
      }else {
        buff[buff_i].r = 0;
        buff[buff_i].g = 0;
        buff[buff_i].b = 0;
        //buff[buff_i] = colors[((data->tid)%8)];
      }
    }
  }
}
/**********************************************************/
/* Updates number of alive cells for a thread partition
*  Params: gol data
*  Returns: number of alive cells in thread partition
*/
int updateLocalAliveCount(struct gol_data *data){
  int i, j, count;

  count = 0;
  for(i = data->rowStart; i <= data->rowStop; i++){
    for(j = data->colStart; j <= data->colStop; j++){
      if(data->board[i*data->cols+j] == 1){
        count++;
      }
    }
  }

  return count;
}
/**********************************************************/
/* the gol application main loop function:
 *  runs rounds of GOL,
 *    * updates program state for next round (world and total_live)
 *    * performs any animation step based on the output/run mode
 *
 *   data: pointer to a struct gol_data  initialized with
 *         all GOL game playing state
 */
void *play_gol(void *args) {

  struct gol_data *data;
  int roundNum, i , j, neighbors, current, localAlive;
  int *temp;
  data = (struct gol_data *)args;

  //printf("I am thread %d.", data->tid);
  if(data->printThreads){
    printThreadPartitionInfo(data);
  }

  // repeat running each round until number of iterations reached
  for(roundNum = 0; roundNum < data->iters; roundNum++){
    // for each cell of thread's partition
    for(i = data->rowStart; i <= data->rowStop; i++){
      for(j = data->colStart; j <= data->colStop; j++){
        // check number of neighbors on board
        neighbors = numNeighbors(data->board, i, j, data->rows, data->cols);
        // update cell accordingly on board copy
        current = data->board[i*data->cols+j];
        if(current == 1){
          if(neighbors <= 1){
            data->boardCopy[i*data->cols+j] = 0;
          }else if(neighbors >= 4){
            data->boardCopy[i*data->cols+j] = 0;
          }else{
           data->boardCopy[i*data->cols+j]=data->board[i*data->cols+j];
          }
        }else if(current == 0 && neighbors == 3){
          data->boardCopy[i*data->cols+j] = 1;
        }else{
          data->boardCopy[i*data->cols+j]= data->board[i*data->cols+j];
        }
      }
    }

    // switch board and board copy
    temp = data->boardCopy;
    data->boardCopy = data->board;
    data->board = temp;

    // update total_live
    // only have thread 0 reset the alive count
    if(data->tid == 0){
      total_live = 0;
    }
    // update own count of alive
    localAlive = updateLocalAliveCount(data);
    // all threads wait for thread 0 to finish resetting total_live
    pthread_barrier_wait(&barrier);

    // update num_alive non-synchronously
    pthread_mutex_lock(&mutex);
    total_live += localAlive;
    pthread_mutex_unlock(&mutex);

    if (data->tid == 0 && data->output_mode == OUTPUT_ASCII){
      system("clear");
      print_board(data, roundNum+1);
      usleep(SLEEP_USECS);
    }
    else if (data->output_mode == OUTPUT_VISI){
      update_colors(data);
      draw_ready(data->handle);
      usleep(SLEEP_USECS);
    }

    // wait for all threads before proceeding to next rounds
    pthread_barrier_wait(&barrier);
  }

  return NULL;
}

/**********************************************************/
/* Print the board to the terminal.
 *   data: gol game specific data
 *   round: the current round number
 *
 */
void print_board(struct gol_data *data, int round) {

  int i, j, r, c;

  /* Print the round number. */
  fprintf(stderr, "Round: %d\n", round);

  r = data->rows;
  c = data->cols;

  for (i = 0; i < r; ++i) {
    for (j = 0; j < c; ++j) {
      //printf("data->board[i][j] = %d\n",data->board[i][j]);
      if(data->board[i*c+j] == 1){
        //printf("here\n");
        fprintf(stderr, " @");
      }else{
        fprintf(stderr, " .");
      }
    }
    fprintf(stderr, "\n");
  }
  //recalculateNumAlive(data);
    /* Print the total number of live cells. */
    fprintf(stderr, "Live cells: %d\n\n", total_live);
}


/**********************************************************/
/***** START: DO NOT MODIFY THIS CODE *****/
/* sequential wrapper functions around ParaVis library functions */
void (*mainloop)(struct gol_data *data);

void* seq_do_something(void * args){
  mainloop((struct gol_data *)args);
  return 0;
}

int connect_animation(void (*applfunc)(struct gol_data *data),
    struct gol_data* data)
{
  pthread_t pid;

  mainloop = applfunc;
  if( pthread_create(&pid, NULL, seq_do_something, (void *)data) ) {
    printf("pthread_created failed\n");
    return 1;
  }
  return 0;
}
/***** END: DO NOT MODIFY THIS CODE *****/
/******************************************************/
