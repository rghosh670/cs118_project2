Names: Botao Xia, Rohit Ghosh
Emails: xiabotao@g.ucla.edu, rohitghosh@g.ucla.edu
UIDs: 805614575, 405279900

High Level Design:

The server code uses the handshake code that was already provided. It waits for a SYN packet and an ACK packet from the client that contains the first payload. It then writes this payload to the file. It then stays in a loop, receiving all the data packets and writing them to the file if it is not a duplicate packet. Once it receives a FIN packet, it proceeds to the last loop which was already provided. The client code uses the code that was already provided to establish a connection. It then reads in the entire file and stores them in packets. The loop after that handles the case where there is only one more packet to send. The one below that one sends packets in a sliding window until all packets are sent. The teardown code is from the skeleton code.

Problems We Ran Into:

We ran into problems with handling when sequence numbers wrap around. To solve for this, we added explicit checks for when this happens. We also ran into problems with too many timeout. To solve this, we created multiple timers for different situations.

All of the libraries we used are from the standard C++ library.