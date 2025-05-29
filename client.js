
const url = 'ws://127.0.0.1:8888/';
const sendAudio = false;

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

    pc.oniceconnectionstatechange = () => console.log(`Connection state: ${pc.iceConnectionState}`);
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
}

connect(url);

