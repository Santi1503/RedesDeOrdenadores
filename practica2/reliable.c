#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "rlib.h"

/*
    Add your own defines below. Remember that you have the following constans
already defined:
    - ACK_PACKET_SIZE: Size of ACK packets
    - DATA_PACKET_HEADER: Data packet's header size
--------------------------------------------------------------------------------
*/
#define STATE_READY_TO_SEND 0
#define STATE_WAIT_ACK 1

//------------------------------------------------------------------------------

/*
    Global data: Add your own data fields below. The information in this global
data is persistent and it can be accesed from all functions.
--------------------------------------------------------------------------------
*/
static int state;                    // Estado de la conexión: esperando ACK o listo para enviar
static uint32_t last_seq_num;        // Último número de secuencia enviado
static uint32_t last_ack_num;        // Último número de ACK recibido
static packet_t *last_packet_sent;   // Apuntador al último paquete enviado
static long timeout_in_ns;           // Tiempo de espera en nanosegundos para retransmisión

//------------------------------------------------------------------------------

/*
    Callback functions: The following functions are called on the corresponding
event as explained in rlib.h file. You should implement these functions.
--------------------------------------------------------------------------------
*/

/*
    Creates a new connection. You should declare any variable needed in the
global data section and make initializations here as required.
*/
void connection_initialization(int window_size, long timeout_ns)
{
	state = STATE_READY_TO_SEND;  // Listo para enviar al principio
    last_seq_num = 0;             // Inicia en el número de secuencia 0
    last_ack_num = 0;             // Sin ACKs aún
    timeout_in_ns = timeout_ns;   // Configura el timeout
    last_packet_sent = NULL;
}

// This callback is called when a packet pkt of size pkt_size is received
void receive_callback(packet_t *pkt, size_t pkt_size)
{
	if (!VALIDATE_CHECKSUM(pkt)) {
        return;  // Si el checksum es inválido, descartar el paquete
    }

    // Verificar si es un paquete de ACK
    if (IS_ACK_PACKET(pkt)) {
        if (pkt->ackno == last_seq_num + 1) {
            // ACK válido, podemos enviar más datos
            last_ack_num = pkt->ackno;
            state = STATE_READY_TO_SEND;
            CLEAR_TIMER(0);  // Limpiar el temporizador ya que se recibió el ACK
            RESUME_TRANSMISSION();  // Reanudar la transmisión
        }
    } else {
        // Es un paquete de datos, enviar un ACK
        SEND_ACK_PACKET(pkt->seqno + 1);
        
        // Aceptar los datos recibidos si es necesario
        ACCEPT_DATA(pkt->data, pkt_size - DATA_PACKET_HEADER);
    }
}

// Callback called when the application has data to be sent
void send_callback()
{
	if (state == STATE_READY_TO_SEND) {
        char data[MAX_PAYLOAD];
        size_t data_len = READ_DATA_FROM_APP_LAYER(data, MAX_PAYLOAD);

        if (data_len <= 0) {
            return;  // No hay datos para enviar
        }

        // Crear el paquete de datos
        packet_t pkt;
        pkt.len = DATA_PACKET_HEADER + data_len;
        pkt.seqno = last_seq_num + 1;  // Número de secuencia

        // Copiar los datos al paquete
        memcpy(pkt.data, data, data_len);

        // Enviar el paquete de datos
        SEND_DATA_PACKET(pkt.len, 0, pkt.seqno, pkt.data);  // No ACK y con secuencia actual

        // Guardar el último paquete enviado y cambiar el estado
        last_packet_sent = malloc(sizeof(packet_t));
        memcpy(last_packet_sent, &pkt, sizeof(packet_t));

        state = STATE_WAIT_ACK;  // Cambia el estado a esperar el ACK
        last_seq_num = pkt.seqno;

        PAUSE_TRANSMISSION();  // Pausa la transmisión hasta recibir el ACK
        SET_TIMER(0, timeout_in_ns);  // Iniciar el temporizador para retransmisión
    }
}

/*
    This function is called when timer timer_number expires. The function of the
timer depends on the protocol programmer.
*/
void timer_callback(int timer_number)
{
	if (timer_number == 0 && state == STATE_WAIT_ACK) {
        // Si expira el temporizador, reenviar el último paquete
        SEND_DATA_PACKET(last_packet_sent->len, 0, last_packet_sent->seqno, last_packet_sent->data);  // Reenviar el último paquete
        SET_TIMER(0, timeout_in_ns);  // Reiniciar el temporizador
    }
}

//------------------------------------------------------------------------------
