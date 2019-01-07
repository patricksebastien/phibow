// require
var osc = require('./omgosc.js');
var geoip = require('geoip');

var City = geoip.City;
var city = new City('/usr/local/lib/node_modules/geoip/GeoLiteCity.dat');


// configure
var io = require('socket.io').listen(80);
var oscAudio = new osc.UdpSender('127.0.0.1', 9998);
var oscVideo = new osc.UdpSender('127.0.0.1', 9999);

// init
var count = 0;
var kfx = [];
for(var i = 1; i <= 20; i++) {
	kfx.push("kfx"+i);
}
var kfxlv = [];
for(var i = 1; i <= 20; i++) {
	kfxlv.push("kfx"+i);
}
var radio1lv;
var radio2lv;

// sockets
io.sockets.on('connection', function (socket) {
	
	var address = socket.handshake.address
	var city_obj = city.lookupSync(address.address);
	oscVideo.send('/geoip', 's', ["New visitor from " + city_obj.country_name]);

	// count number of concurrent visitors
	count++;
	io.sockets.emit('nbVisitor', { nbVisitor: count });
	oscAudio.send('/nbvisitor', 'f', [count]);

	socket.on('disconnect', function(){
		count--;
		io.sockets.emit('nbVisitor', { nbVisitor: count });
		oscAudio.send('/nbvisitor', 'f', [count]);
	})

	// send current state to new visitor
	kfx.forEach(function(ev) {
		socket.emit(ev, { ive: kfxlv[ev] });
	});
	socket.emit('radio1', { ive: radio1lv });
	socket.emit('radio2', { ive: radio2lv });
	
	// broadcast gui change to visitor and pd
	kfx.forEach(function(ev) {
		socket.on(ev, function(data) {
			kfxlv[ev] = data['ive'];
			socket.broadcast.emit(ev, { ive: data['ive']});
			oscAudio.send('/kfx/'+ev, 'f', [data['ive']]);
		});
	});

	socket.on('radio1', function (data) {
		radio1lv = data['ive']; 
		socket.broadcast.emit('radio1', { ive: data['ive']});
		oscVideo.send('/radio1', 'f', [data['ive']]);
	});

	socket.on('radio2', function (data) {
		radio2lv = data['ive'];
		socket.broadcast.emit('radio2', { ive: data['ive']});
		oscAudio.send('/radio2', 'f', [data['ive']]);
	});
	
	socket.on('piano', function (data) {
		socket.broadcast.emit('piano', { ive: data['ive']});
		oscAudio.send('/piano', 'f', [data['ive']]);
	});
	
	socket.on('chat', function (data) {
		oscVideo.send('/chat', 's', [data['ive']]);
		oscAudio.send('/chat', 's', [data['ive']]);
	});
});

