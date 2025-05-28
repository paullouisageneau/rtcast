
const url = 'ws://10.9.0.205:8888/';
const sendAudio = true;

let ws = null;
let pc = null;
let dc = null;

async function connect(url) {
  let stream = null;
  if (sendAudio) {
    stream = await navigator.mediaDevices.getUserMedia({
      audio: true,
    });
  }

  ws = new WebSocket(url);

  ws.onopen = () => {
    const config = {
      bundlePolicy: "max-bundle",
      iceServers: [{
        urls: ['stun:stun.l.google.com:19302'],
      }],
    };

    pc = new RTCPeerConnection(config);

    pc.oniceconnectionstatechange = () => {
        console.log(`Connection state: ${pc.iceConnectionState}`);
        if (pc.iceConnectionState == 'failed')
            alert('Connection failed.');
    };
    pc.onicegatheringstatechange = () => console.log(`Gathering state: ${pc.iceGatheringState}`);
    pc.onsignalingstatechange = () => console.log(`Signaling state: ${pc.signalingState}`);

    pc.onicecandidate = (evt) => {
      if (!evt.candidate)
        return;

      const { candidate, sdpMid } = evt.candidate;
      if (!candidate)
        return;

      ws.send(JSON.stringify({
        type: 'candidate',
        candidate: candidate,
        mid: sdpMid,
      }));
    };

    pc.ontrack = (evt) => {
      console.log("Received track");
      const video = document.getElementById('video');
      const [stream] = evt.streams;
      video.srcObject = stream;
      video.play().catch(() => {});
    };

    pc.ondatachannel = (evt) => {
      console.log("Received data channel");
      dc = evt.channel;
      dc.onmessage = (evt) => console.log(`Message received: ${evt.data}`);
    };

    if (stream)
      for (const track of stream.getTracks())
        pc.addTrack(track);
  };

  ws.onmessage = async (evt) => {
    if (typeof evt.data !== 'string')
      return;

    const message = JSON.parse(evt.data);
    const type = message.type;
    if (type == 'offer' || type == 'answer') {
      const sdp = message.description;
      await pc.setRemoteDescription({
        type,
        sdp,
      });
      if (type == 'offer') {
        const answer = await pc.createAnswer();
        await pc.setLocalDescription(answer);
        ws.send(JSON.stringify({
          description: answer.sdp,
          type: answer.type,
        }));
      }
    } else if (type == 'candidate') {
      const { candidate, mid } = message;
      await pc.addIceCandidate({
        candidate,
        sdpMid: mid,
      });
    }
  };

  ws.onerror = (evt) => {
    alert('Telebot is offline.');
  };
}

let controls = {};

function setupControl() {
  const directions = ['up', 'down', 'left', 'right'];
  const keyCodes = [38, 40, 37, 39];

  let arrows = {}; // TODO

  for (dir in directions) {
    if (arrows[dir]) {
	    arrows[dir].onmousedown = (evt) => {
		    evt.preventDefault();
        if(!controls[dir]) {
          controls[dir] = true;
          updateControl();
        }
      };

      arrows[dir].onmouseup = (evt) => {
        evt.preventDefault();
        controls[dir] = false;
        updateControl();
      };
    }

	  if('ontouchstart' in document.documentElement) {
      arrows[dir].ontouchstart = arrows[dir].onmousedown;
      arrows[dir].ontouchend = arrows[dir].onmouseup;
	  }
  }

  document.onkeydown = (evt) => {
    const i = keyCodes.indexOf(evt.keyCode);
    if (i >= 0) {
      evt.preventDefault();
	    dir = directions[i];
      if(!controls[dir]) {
        controls[dir] = true;
        updateControl();
      }
    }
  };

  document.onkeyup = (evt) => {
    const i = keyCodes.indexOf(evt.keyCode);
    if (i >= 0) {
	    evt.preventDefault();
      dir = directions[i];
	    controls[dir] = false;
	    updateControl();
    }
  };
}

gamepad = null;
gamepadx = 0;
gamepady = 0;

function checkGamepad() {
  if (!gamepad) {
    gamepadx = 0;
    gamepady = 0;
    updateControl();
    return;
  }

  threshold = 0.1;
  gamepadx = Math.abs(gamepad.axes[0]) >= threshold ? gamepad.axes[0] : 0;
  gamepady = Math.abs(gamepad.axes[1]) >= threshold ? gamepad.axes[1] : 0;

  updateControl();
  setTimeout(checkGamepad, 100);
}

function updateControl() {
  let left  = - gamepady + gamepadx;
  let right = - gamepady - gamepadx;

  if (controls['up']) {
    left += 1.0;
    right+= 1.0;
  }
  if(controls['down']) {
    left += -1.0;
    right+= -1.0;
  }
  if(controls['left']) {
    left = Math.min(left  - 1.0, 0);
    right= Math.max(right + 1.0, 0);
  }
  if(controls['right']) {
    left = Math.max(left  + 1.0, 0);
    right= Math.min(right - 1.0, 0);
  }

  const power = 100.0;
  left  = Math.round(Math.min(Math.max(left,  -1), 1)*power);
  right = Math.round(Math.min(Math.max(right, -1), 1)*power);

  const message = {
    control: {
      left: left,
      right: right
    }
  };

  if (dc)
    dc.send(JSON.stringify(message));
}

window.addEventListener('gamepadconnected', function(e) {
  console.log(`Gamepad: ${e.gamepad.id}`);
  gamepad = e.gamepad;
  window.requestAnimationFrame(checkGamepad);
});

window.addEventListener('gamepaddisconnected', function(e) {
  if (e.gamepad.index == gamepad.index) {
    console.log("Gamepad disconnected");
    gamepad = null;
  }
});


window.onload = (evt) => {
  setupControl();
  connect(url);
};

