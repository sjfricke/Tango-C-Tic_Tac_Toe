var http = require('http');
var server = http.createServer(function(request, response) {});
var _port = process.env.port || 8080;

server.listen(_port, function() {
    console.log((new Date()) + ' Server is listening on port' + _port);
});

var WebSocketServer = require('websocket').server;
wsServer = new WebSocketServer({
    httpServer: server
});

var count = 0;
var clients = {};

wsServer.on('request', function(r){
    // Code here to run on connection
    var connection = r.accept('echo-protocol', r.origin);

    // Specific id for this client & increment count
    var id = count++;
    // Store the connection method so we can loop through & contact all clients
    clients[id] = connection

    console.log((new Date()) + ' Connection accepted [' + id + ']');

    // Create event listener
    connection.on('message', function(message) {

	// The string message that was sent to us
	var msgString = message.utf8Data;

	// Loop through all clients
	for(var i in clients){
	    // Send a message to the client with the message
	    clients[i].sendUTF(msgString);
	}

    });
});
