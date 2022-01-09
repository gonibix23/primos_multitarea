#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>

#define LONGITUD_MSG 100           // Payload del mensaje
#define LONGITUD_MSG_ERR 200       // Mensajes de error por pantalla

// Códigos de exit por error
#define ERR_ENTRADA_ERRONEA 2
#define ERR_SEND 3
#define ERR_RECV 4
#define ERR_FSAL 5

#define NOMBRE_FICH "primos.txt"
#define NOMBRE_FICH_CUENTA "cuentaprimos.txt"
#define CADA_CUANTOS_ESCRIBO 5

// rango de búsqueda, desde BASE a BASE+RANGO
#define BASE 800000000
#define RANGO 2000

// Intervalo del temporizador para RAIZ
#define INTERVALO_TIMER 5

// Códigos de mensaje para el campo mesg_type del tipo T_MESG_BUFFER
#define COD_ESTOY_AQUI 5           // Un calculador indica al SERVER que está preparado
#define COD_LIMITES 4              // Mensaje del SERVER al calculador indicando los límites de operación
#define COD_RESULTADOS 6           // Localizado un primo
#define COD_FIN 7                  // Final del procesamiento de un calculador

// Mensaje que se intercambia

typedef struct {
    long mesg_type;
    char mesg_text[LONGITUD_MSG];
} T_MESG_BUFFER;

int Comprobarsiesprimo(long int numero);
void Informar(char *texto, int verboso);
void Imprimirjerarquiaproc(int pidraiz,int pidservidor, int *pidhijos, int numhijos);
int ContarLineas();
static void alarmHandler(int signo);

int main(int argc, char* argv[])
{
	int i,j;
	long int numero;
	long int numprimrec;
    long int nbase;
    int nrango;
    int nfin;
    time_t tstart,tend; 
	
	key_t key;
    int msgid;    
    int pid, pidservidor, pidraiz, parentpid, mypid, pidcalc;
    int *pidhijos;
    int intervalo,inicuenta;
    int verbosity;
    T_MESG_BUFFER message;
    char info[LONGITUD_MSG_ERR];
    FILE *fsal, *fc;
    int numhijos;
	int numprimos = 0; // Variable para contar el numero de primos encontrados

    // Control de entrada, después del nombre del script debe figurar el número de hijos y el parámetro verbosity

    numhijos = atoi(argv[1]);
	verbosity = atoi(argv[2]);

    pid=fork();       // Creación del SERVER
    
    if (pid == 0) { // Rama del hijo de RAIZ (SERVER)

		pid = getpid();
		pidservidor = pid;
		mypid = pidservidor;	   
		
		// Petición de clave para crear la cola
		if ( ( key = ftok( "/tmp", 'C' ) ) == -1 ) {
			perror( "Fallo al pedir ftok" );
			exit( 1 );
		}
		
		printf( "Server: System V IPC key = %u\n", key );

        // Creación de la cola de mensajería
		if ( ( msgid = msgget( key, IPC_CREAT | 0666 ) ) == -1 ) {
			perror( "Fallo al crear la cola de mensajes" );
			exit( 2 );
		}
		printf("Server: Message queue id = %u\n", msgid );

        i = 0;
        // Creación de los procesos CALCuladores
		while(i < numhijos) {
			if(pid > 0) { // Solo SERVER creará hijos
				pid=fork(); 
				if (pid == 0) {   // Rama hijo
					parentpid = getppid();
					mypid = getpid();
				}
			}
		i++;  // Número de hijos creados
		}

        // AQUI VA LA LOGICA DE NEGOCIO DE CADA CALCulador. 
		if (mypid != pidservidor) {

			message.mesg_type = COD_ESTOY_AQUI;
			sprintf(message.mesg_text,"%d",mypid);
			msgsnd( msgid, &message, sizeof(message), IPC_NOWAIT);
		
			// Un montón de código por escribir
			//Recibir principio y fin de búsqueda de primos
			msgrcv(msgid, &message, sizeof(message), COD_LIMITES, 0);
			sscanf(message.mesg_text,"%ld %d",&nbase, &intervalo);
			sleep(1);
			//Búsqueda de los números primos
			for (numero = nbase; numero < nbase+intervalo; numero++) {
				if (Comprobarsiesprimo(numero)){
					message.mesg_type = COD_RESULTADOS;
					sprintf(message.mesg_text,"%d:  %ld", mypid, numero);
					msgsnd(msgid, &message, sizeof(message), IPC_NOWAIT);
				}
			}
			//Envío del mensaje de fin de búsqueda
			message.mesg_type = COD_FIN;
			sprintf(message.mesg_text,"%d", mypid);
			msgsnd( msgid, &message, sizeof(message), IPC_NOWAIT);

			exit(0);
		
		} else { // SERVER

			// Pide memoria dinámica para crear la lista de pids de los hijos CALCuladores
			pidhijos = (int*)malloc(numhijos*sizeof(int));
			time(&tstart);
			//Recepción de los mensajes COD_ESTOY_AQUI de los hijos
			for (j = 0; j < numhijos; j++) {
				msgrcv(msgid, &message, sizeof(message), 0, 0);
				sscanf(message.mesg_text,"%d",&pid); // Tendrás que guardar esa pid
				*(pidhijos+j) = pid;
			}
			// Mucho código con la lógica de negocio de SERVER
			//Imprimir Jerarquía
			pidraiz = getppid();
			Imprimirjerarquiaproc(pidraiz, pidservidor, pidhijos, numhijos);

			//Enviar principio y fin de búsqueda de primos
			intervalo = RANGO/numhijos;
			nbase = BASE;
			for (j = 0; j < numhijos; j++) {
				message.mesg_type = COD_LIMITES;
				sprintf(message.mesg_text,"%ld %d", nbase, intervalo);
				msgsnd( msgid, &message, sizeof(message), IPC_NOWAIT);
				nbase = nbase+intervalo;
			}

			//Recibir e imprimir los números primos
			fsal = fopen(NOMBRE_FICH, "w");
			nfin = 0;
			while(nfin < numhijos) {
				if(msgrcv(msgid, &message, sizeof(message), 0, 0)) {
					if(message.mesg_type == COD_RESULTADOS) {
						Informar(message.mesg_text, verbosity);
						sscanf(message.mesg_text,"%d:  %ld", &pid, &numprimrec);
						fprintf(fsal, "%ld\n", numprimrec);
						numprimos++;
						if(numprimos%CADA_CUANTOS_ESCRIBO == 0 && numprimos != 0) {
							fc = fopen(NOMBRE_FICH_CUENTA, "w");
							fprintf(fc, "%d\n", numprimos);
							fclose(fc);
						}
					}
					else if(message.mesg_type == COD_FIN) {
						nfin++;
						printf("El proceso %s ha terminado\n", message.mesg_text);
					}
				}
				
			}
			fclose(fsal);
			printf("Calculos terminados\n");
			time(&tend);
			printf("Números encontrados: %d.\n", numprimos);
			printf("Tiempo total del calculo: %.2f segundos.\n", difftime(tend, tstart));
			msgctl(msgid, IPC_RMID, NULL); // Borrar la cola de mensajería, muy importante
	   	}

    } else { // Rama de RAIZ, proceso primigenio
		alarm(INTERVALO_TIMER);
		signal(SIGALRM, alarmHandler);
		wait(NULL);
		free(pidhijos);
		
    }
}

// Manejador de la alarma en el RAIZ
static void alarmHandler(int signo) {
	FILE *fc = fopen(NOMBRE_FICH_CUENTA, "r");
	if(fc){
		int cuentaLineas;
		fscanf(fc, "%d", &cuentaLineas);
		printf("%d\n", cuentaLineas);
		fclose(fc);
	}
	alarm(INTERVALO_TIMER);
}

void Imprimirjerarquiaproc(int pidraiz,int pidservidor, int *pidhijos, int numhijos) {
	printf("Raiz\tServ\tCalc\n");
	printf("%d\t%d\t%d\t\n", pidraiz, pidservidor, *pidhijos);
	for(int i=1; i<numhijos; i++){
		printf("\t\t%d\n", *(pidhijos+i));
	}
}

int Comprobarsiesprimo(long int numero) {
  if (numero < 2) return 0; // Por convenio 0 y 1 no son primos ni compuestos
  else
	for (int x = 2; x <= (numero / 2) ; x++)
		if (numero % x == 0) return 0;
  return 1;
}

void Informar(char *texto, int verboso) {
	if(verboso){
		printf("%s\n", texto);
	}
}
