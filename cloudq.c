#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "rvgs.h"
#include "rngs.h"
#include "basic.h"
#include "utils.h"
#include "queue.h"
#include "eventq.h"
#include "rvms.h"

double GetArrival(int *j)
{
	const double mean[2] = {1/L1, 1/L2};	
	static double arrival[2] = {START, START};
	static int init = 1;
	double temp;
	
	if (init) {
		SelectStream(0);
		arrival[0] += Exponential(mean[0]);
		SelectStream(1);
		arrival[1] += Exponential(mean[1]);
		init=0;
	}

	if (arrival[0] <= arrival[1])
		*j = 0;
	else
		*j = 1;

	temp = arrival[*j];
	SelectStream(*j);
	arrival[*j] += Exponential(mean[*j]);

	return temp;
}



double GetService(int j, int n)
{
	const double mean[4] = {1/M1CLET, 1/M2CLET,
                            1/M1CLOUD, 1/M2CLOUD};	
    SelectStream(j + n + 2);
    return Exponential(mean[j + n]);
}



double GetSetup()
{
    return Exponential(SETUP);
}



double srvjob(struct event *e, int node, struct queue_t *queue, clock t)
{
    double service = GetService(e->job, node);
    struct event *new = alloc_event();

	*new=*e;
    new->time = t.current + service;
    new->s_start = t.current;
    new->type = E_DEPART;
    new->node = node;
    new->batch = e->batch;
    enqueue_event(new, queue);

    return service;
}


/* return the cloudlet service time of the removed job */
double rplcjob(struct queue_t *queue, clock t, double *s_int, unsigned int n)
{
    double setup = GetSetup();
    struct event *e = alloc_event();
    struct event *removed = NULL;
    double service;
    
    e->job = J_CLASS2;
    e->type = E_DEPART;
    e->node = CLET;
    removed = remove_event(queue, e, n);
    service = removed->time - removed->s_start;
    *s_int += t.current - removed->s_start;

    // TODO: reinserire removed e togliere le assegnazioni
	e->s_start = removed->s_start;
	e->batch = removed->batch;
    e->time = t.current + setup;
    e->node = CLOUD;
    e->type = E_SETUP;
    enqueue_event(e, queue);
    free(removed);

    return service;
}


double autocor(double *data, size_t size, unsigned int lag)
{
    double sum = 0.0;
    double cosum = 0.0;
    double cosum_0 = 0.0;
    double mean;
    unsigned int i;

    for (i = 0; i < size - lag; i++) {
        cosum += data[i] * data[i+lag];
        cosum_0 += data[i] * data[i];
        sum += data[i];
    }
    for (i = size - lag; i < size; i++) {
        cosum_0 += data[i] * data[i];
        sum += data[i];
    }
    mean = sum / size;
    cosum = (cosum / (size - lag)) - (mean * mean);
    cosum_0 = (cosum_0 / size) - (mean * mean);

    return cosum / cosum_0;
}


struct conf_int calcola_intDiConf(double *batch_mean, long k, double alpha){
	double sample_mean=0.0, dev_std=0.0;
	struct conf_int c_int;
	
	int i;
	for(i=0; i<k; i++){
    	sample_mean += batch_mean[i];
    	//printf("bm = %f\n", batch_mean[i]);
    	//fprintf(stderr, "%f\n", batch_mean[i]);
    }
    sample_mean = sample_mean/k;	//media campionaria ottenuta dalle medie camp dei vari batch
    
    for(i=0; i<k; i++){
		dev_std += pow(batch_mean[i] - sample_mean, 2);
	}
	dev_std = sqrt(dev_std/k);

	//VALORE CRITICO t*
	//TODO: controllare valore parametro n,k
	double t = idfStudent(k-1, 1-alpha/2);
	//printf("t = idfStudent è %6.2f\n", t);
	/*
	t = idfNormal(0.0, 1.0, 1-alpha/2);
	printf("t = idfNormal è %6.2f\n", t);
	*/

	/*
	//INTERVALLO
	double int1, int2;
	int1 = sample_mean - (t*dev_std)/sqrt(k-1);
	int2 = sample_mean + (t*dev_std)/sqrt(k-1);
	printf("intervallo : %f , %f\n", int1, int2);
	*/

	//LUNGHEZZA DELL'INTERVALLO
	double w = (t*dev_std)/sqrt(k-1);
	/*
	printf("media campionaria = %f\n", sample_mean);
	printf("dev_std = %f\n", dev_std);
	printf("t*dev_std = %f\n", t*dev_std);
	printf("sqrt(n-1) = %f\n", sqrt(k-1));
	printf("w = %6.2f\n", w); 
	*/ 
	
	c_int.w = w;
	c_int.sample_mean = sample_mean;
	return c_int;  
}



int main(void)
{
    clock t;
    int jobclass;
    
    long n[4];          /* local population */
    long n_setup;       /* setup population */

    long n_int;         /* total # of interrupted jobs  */

    long arrived;       /* global arrivals  */
    long a[4];          /* local arrivals   */

    long completions;   /* global completions                   */
    long compl_stop;    /* glogal completions in [START, STOP]  */
    long c[4];          /* local completions                    */
    long c_stop[4];     /* local completions in [START, STOP]   */ 

    double service;     /* global service time  */
    double s[4];        /* local service time   */
    double si_clet;     /* interrupted jobs' cloudlet service time  */
    double si_cloud;    /* interrupted jobs' cloud service time     */
    double s_setup;     /* interruptes jobs' setup time             */

    double area_tot;    /* time integrated # in the system */
    double area[4];
    double area_setup;

	double t_start;
	double t_stop;

    double ac;          /* autocorrelation between batches */

    long seed;

    int i;      // array index
    int r;      // replication index
    char *node;
	
	//BATCH
	//k>=32, k=64 raccomandato. b almeno 2 volte l'aurocorrelation cut-off lag
	double alpha = 0.05;
	//TODO: aggiungere controllo su ultimo batch. n da tastiera, e non ricavato da b*k
	long k = 64;	        //numero dei batch
	long b = 4000;	        //lunghezza dei batch
	long n_job = b * k;	    //numero di job
	long cont;	            //contatore per numero job in arrivo
	long batch;	            //batch attualmente assegnato
	double batch_mean[k];	//array contenente le medie dei vari batch. Viene aggiornato al completamento dei job
    struct conf_int s_time[R];
	
	printf("numero job = %ld,  k= %ld,  b=%ld\n", n_job, k, b);
	
	for (i = 0; i < k; i++)
		batch_mean[i] = 0.0;
	
    struct event *e;
    struct queue_t queue;

    /* initialize event queue */
    queue.head = queue.tail = NULL;
    /* initialize simulation clock */
    t.current = START;

    printf("Enter initial seed: ");
    if (scanf("%ld", &seed) > 9)
        handle_error("bad input");
    

    for (r = 0; r < R; r++, GetSeed(&seed)) {

	    cont = 0;	
	    batch = 0;
        n_setup = 0;           
        n_int = 0;            
        arrived = 0;         
        completions = 0;           
        compl_stop = 0;            
        service = 0;                 
        si_clet = 0.0;           
        si_cloud = 0.0;          
        s_setup = 0.0;           
        area_tot = 0.0;         
        area_setup = 0.0;
        t_start = t.current;
        t_stop = INFINITY;

        for (i = 0; i < 4; i++) {
            n[i] = 0;       
            a[i] = 0;   
            c[i] = 0;       
            c_stop[i] = 0;  
            s[i] = 0.0; 
            area[i] = 0.0;
        }


        PlantSeeds(seed); 
        e = alloc_event();
        e->time = GetArrival(&jobclass);
        e->type = E_ARRIVL;
        e->job = jobclass;

        enqueue_event(e, &queue);


        while (queue.head != NULL) {

            e = dequeue_event(&queue); 
            t.next = e->time;                               /* next event time */
            
            for (i = 0; i < 4; i++)                         /* update integral  */
                area[i] += (t.next - t.current) * n[i];   
            area_setup += (t.next - t.current) * n_setup;              

            t.current = t.next;                             /* advance the clock */


            switch (e->type) {

            case E_ARRIVL:                                  /* process an arrival */

            //BATCH
                arrived++;
                cont++;
                if (cont < b)
                    e->batch = batch;
                else {
                    cont = 0;
                    batch++;
                }

                if (e->job == J_CLASS1) {            /* process a class 1 arrival */
                    //fprintf(stderr, "class 1 arrival\n");
                    if (n[J_CLASS1 + CLET] == N) {
                        s[J_CLASS1 + CLOUD] += srvjob(e, CLOUD, &queue, t);
                        a[J_CLASS1 + CLOUD]++;
                        n[J_CLASS1 + CLOUD]++;
                    }
                    else if (n[J_CLASS1 + CLET] + n[J_CLASS2 + CLET] < S) { // accept job
                            s[J_CLASS1 + CLET] += srvjob(e, CLET, &queue, t);
                            a[J_CLASS1 + CLET]++;
                            n[J_CLASS1 + CLET]++;
                        }
                        else if (n[J_CLASS2 + CLET] > 0) { // replace a class 2 job 
                                s[J_CLASS2 + CLET] -= rplcjob(&queue, t, &si_clet, n[J_CLASS2 + CLET]);
                                s[J_CLASS1 + CLET] += srvjob(e, CLET, &queue, t);
                                a[J_CLASS1 + CLET]++;
                                n[J_CLASS1 + CLET]++;
                                n[J_CLASS2 + CLET]--;
                                n_setup++;
                                n_int++;
                            }
                            else { // accept job
                                s[J_CLASS1 + CLET] += srvjob(e, CLET, &queue, t);
                                a[J_CLASS1 + CLET]++;
                                n[J_CLASS1 + CLET]++;
                            }
                }
                else {                 /* process a class 2 arrival */
                    //fprintf(stderr, "class 2 arrival\n");
                    if (n[J_CLASS1 + CLET] + n[J_CLASS2 + CLET] >= S) {
                        s[J_CLASS2 + CLOUD] += srvjob(e, CLOUD, &queue, t);
                        a[J_CLASS2 + CLOUD]++;
                        n[J_CLASS2 + CLOUD]++;
                    }
                    else {
                        s[J_CLASS2 + CLET] += srvjob(e, CLET, &queue, t);
                        a[J_CLASS2 + CLET]++;
                        n[J_CLASS2 + CLET]++;
                    }
                }
                e->time = GetArrival(&jobclass); /* set next arrival t */
                e->job = jobclass;               /* set next arrival j */
                
                //if (e->time <= STOP) 
                if (arrived >= n_job) {
                    e->type = E_IGNRVL; // arrival to not process
                    t_stop = t.current; 
                }
                enqueue_event(e, &queue);
                /*
                fprint_queue(stderr, &queue, fprint_clet);
                fprintf(stderr, "cloudlet jobs: n1 = %ld, n2 = %ld\n\n", 
                        n[J_CLASS1 + CLET], n[J_CLASS2 + CLET]);
                fprint_queue(stderr, &queue, fprint_cloud);
                fprintf(stderr, "cloud jobs: n1 = %ld, n2 = %ld\n\n", 
                        n[J_CLASS1 + CLOUD], n[J_CLASS2 + CLOUD]);
                */

                break;

           case E_IGNRVL:
                //fprintf(stderr, "ignored arrival\n");
                if (n[J_CLASS1 + CLET] + n[J_CLASS2 + CLET] + 
                    n[J_CLASS1 + CLOUD] + n[J_CLASS2 + CLOUD] + 
                    n_setup) { // there are jobs to be processed
                    e->time = GetArrival(&jobclass); /* set next arrival to ignore */
                    enqueue_event(e, &queue);
                }
                else free(e);
                break;
 
                /*
                fprint_queue(stderr, &queue, fprint_clet);
                fprintf(stderr, "cloudlet jobs: n1 = %ld, n2 = %ld\n\n", 
                        n[J_CLASS1 + CLET], n[J_CLASS2 + CLET]);
                fprint_queue(stderr, &queue, fprint_cloud);
                fprintf(stderr, "cloud jobs: n1 = %ld, n2 = %ld\n\n", 
                        n[J_CLASS1 + CLOUD], n[J_CLASS2 + CLOUD]);
                */               
           case E_SETUP:
                //fprintf(stderr, "setup\n");
                si_cloud += srvjob(e, CLOUD, &queue, t);
                n[J_CLASS2 + CLOUD]++;
                n_setup--;

                /*
                fprint_queue(stderr, &queue, fprint_clet);
                fprintf(stderr, "cloudlet jobs: n1 = %ld, n2 = %ld\n\n", 
                        n[J_CLASS1 + CLET], n[J_CLASS2 + CLET]);
                fprint_queue(stderr, &queue, fprint_cloud);
                fprintf(stderr, "cloud jobs: n1 = %ld, n2 = %ld\n\n", 
                        n[J_CLASS1 + CLOUD], n[J_CLASS2 + CLOUD]);
                */

                break;

            case E_DEPART:                /* process a departure */
                
                node = e->node == CLET ? "cloudlet" : "cloud";
                //fprintf(stderr, "class %d departure from %s\n", e->job + 1, node);

                n[e->job + e->node]--;
                c[e->job + e->node]++;

                if (t.current <= t_stop)
                    c_stop[e->job + e->node]++;

                //BATCH
                batch_mean[e->batch] += e->time - e->s_start;
                //printf("batch index = %d, service time = %f\n", e->batch, e->time - e->s_start);
                free(e);

                /*
                fprint_queue(stderr, &queue, fprint_clet);
                fprintf(stderr, "cloudlet jobs: n1 = %ld, n2 = %ld\n\n", 
                        n[J_CLASS1 + CLET], n[J_CLASS2 + CLET]);
                fprint_queue(stderr, &queue, fprint_cloud);
                fprintf(stderr, "cloud jobs: n1 = %ld, n2 = %ld\n\n", 
                        n[J_CLASS1 + CLOUD], n[J_CLASS2 + CLOUD]);
                */

                break;
            
            default:
                handle_error("unknown event type");
            }
        }
        
        //BATCH MEAN
        //calcolo le medie campionarie
        
        for(i = 0; i < k; i++){
            //printf("%f\n", batch_mean[i]);
            batch_mean[i] = batch_mean[i]/b;
            //printf("%f\n", batch_mean[i]);
        }


        ac = autocor(batch_mean, k, 1);
        printf("Autocorrelation lag 1 = %f\n", ac);
         
        s_time[r] = calcola_intDiConf(batch_mean, k, alpha);
        //printf("w = %f, sample_mean %f\n", c_int.w, c_int.sample_mean);

        /****************** print results *****************/

        for (i = 0; i < 4; i++) {
            //arrived += a[i];
            completions += c[i];
            compl_stop += c_stop[i];
            area_tot += area[i];
            service += s[i];
        }
        area_tot += area_setup;
        service += (si_clet + si_cloud + s_setup);
        

        printf("\nSimulation run with N=%d, S=%d, t_start=%.2f, t_stop=%.2f\n\n", 
                N, S, t_start, t_stop);

        printf("\nSystem statistics\n\n");
        printf("  Arrived jobs ....................... = %ld\n", arrived);
        printf("  Processed jobs ..................... = %ld\n", completions);

        printf("  Arrival rate ....................... = %f\n", arrived / (t_stop - t_start));
        printf("  Departure rate ..................... = %f\n\n", compl_stop / (t_stop - t_start));

        printf("  Class 1 system response time ....... = %f\n", 
            (s[J_CLASS1 + CLET] + s[J_CLASS1 + CLOUD]) / 
            (a[J_CLASS1 + CLET] + a[J_CLASS1 + CLOUD]));
        printf("  Class 2 system response time ....... = %f\n", 
            (s[J_CLASS2 + CLET] + s[J_CLASS2 + CLOUD] + si_clet + si_cloud + s_setup) / 
            (c[J_CLASS2 + CLET] + c[J_CLASS2 + CLOUD]));
        printf("  Global system response time ........ = %f\n", service / arrived);
        printf("  System mean population ............. = %f\n", area_tot / (t.current - t_start));
        printf("  Little's Law: arrival rate = N/S ... = %f (%.2f)\n\n", 
                area_tot * arrived / service / (t.current - t_start), L1 + L2);

        printf("  Class 1 system throughput .......... = %f\n", 
            (c_stop[J_CLASS1 + CLET] + c_stop[J_CLASS1 + CLOUD]) / (t_stop - t_start));
        printf("  Class 2 system throughput .......... = %f\n",  
            (c_stop[J_CLASS2 + CLET] + c_stop[J_CLASS2 + CLOUD]) / (t_stop - t_start));
        printf("  Global system throughput ........... = %f\n\n", 
            compl_stop / (t_stop - t_start));
            
        printf("  Class 1 cloudlet throughput ........ = %f\n", 
            c_stop[J_CLASS1 + CLET] / (t_stop - t_start));
        printf("  Class 2 cloudlet throughput ........ = %f\n", 
            c_stop[J_CLASS2 + CLET] / (t_stop - t_start));
        printf("  Global cloudlet throughput ......... = %f\n\n", 
            (c_stop[J_CLASS1 + CLET] + c_stop[J_CLASS2 + CLET]) / (t_stop - t_start));
        
        printf("  Class 2 arrivals on the cloudlet ... = %ld\n", a[J_CLASS2 + CLET]);
        printf("  Interrupted tasks .................. = %ld\n", n_int);
        printf("  Interrupted tasks percentage ....... = %f\n", (double) 
            n_int / a[J_CLASS2 + CLET]);
        printf("  Interrupted tasks response time .... = %f\n\n", 
            (si_clet + s_setup + si_cloud) / n_int);


        printf("  Class 1 cloudlet response time ..... = %f\n", 
            s[J_CLASS1 + CLET] / a[J_CLASS1 + CLET]);
        printf("  Consistency check: M1CLET = 1/S = .. = %f (%.2f)\n", 
            a[J_CLASS1 + CLET] / s[J_CLASS1 + CLET], M1CLET);

        printf("  Class 1 cloud response time ........ = %f\n", 
            s[J_CLASS1 + CLOUD] / a[J_CLASS1 + CLOUD]);
        printf("  Consistency check: M1CLOUD = 1/S ... = %f (%.2f)\n", 
            a[J_CLASS1 + CLOUD] / s[J_CLASS1 + CLOUD], M1CLOUD);

        printf("  Class 2 cloudlet response time ..... = %f\n", 
            s[J_CLASS2 + CLET] / c[J_CLASS2 + CLET]);
        printf("  Consistency check: M2CLET = 1/S .... = %f (%.2f)\n", 
            c[J_CLASS2 + CLET] / s[J_CLASS2 + CLET], M2CLET);

        printf("  Class 2 cloud response time ........ = %f\n", 
            (s[J_CLASS2 + CLOUD] + si_cloud) / c[J_CLASS2 + CLOUD]);
        printf("  Consistency check: M2CLOUD = 1/S ... = %f (%.2f)\n", 
            c[J_CLASS2 + CLOUD] / (s[J_CLASS2 + CLOUD] + si_cloud), M2CLOUD);


        printf("  Class 1 cloudlet mean population ... = %f\n", 
            area[J_CLASS1 + CLET] / (t.current - t_start));
        printf("  Class 1 cloud mean population ...... = %f\n", 
            area[J_CLASS1 + CLOUD] / (t.current - t_start));
        printf("  Class 2 cloudlet mean population ... = %f\n", 
            area[J_CLASS2 + CLET] / (t.current - t_start));
        printf("  Class 2 cloud mean population ...... = %f\n\n", 
            area[J_CLASS2 + CLOUD] / (t.current - t_start));

        
        printf("  Class 1 cloudlet arrivals ... = %ld\n", a[J_CLASS1 + CLET]);
        printf("  Class 2 cloudlet arrivals ... = %ld\n", a[J_CLASS2 + CLET]); 
        printf("  Class 1 cloud arrivals ...... = %ld\n", a[J_CLASS1 + CLOUD]);
        printf("  Class 2 cloud arrivals ...... = %ld\n\n", a[J_CLASS2 + CLOUD]);

        printf("  Class 1 cloudlet completions ... = %ld\n", c[J_CLASS1 + CLET]);
        printf("  Class 2 cloudlet completions ... = %ld\n", c[J_CLASS2 + CLET]); 
        printf("  Class 1 cloud completions ...... = %ld\n", c[J_CLASS1 + CLOUD]);
        printf("  Class 2 cloud completions ...... = %ld\n\n", c[J_CLASS2 + CLOUD]);
       

    }

    printf("replication     mean        w\n");
    for (r = 0; r < R; r++) {
        printf("  %d            %.3f      %.3f\n", 
            r + 1, s_time[r].sample_mean, s_time[r].w);
        fprintf(stderr, "%d %f %f\n", 
            r + 1, s_time[r].sample_mean, s_time[r].w);
    }

    return EXIT_SUCCESS;
}
