/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 10000;

SpecialPowers.setBoolPref("dom.sms.enabled", true);
SpecialPowers.addPermission("sms", true, document);

const REMOTE = "5555552368";
const SENDER = "+15555552368"; // the normalized remote number
const RECEIVER = "+15555215554"; // the emulator's number

let sms = window.navigator.mozSms;
let body = "Hello SMS world!";
let now = Date.now();

let completed = false;
runEmulatorCmd("sms send " + REMOTE + " " + body, function(result) {
  log("Sent fake SMS: " + result);
  is(result[0], "OK");
  completed = true;
});

sms.onreceived = function onreceived(event) {
  log("Received an SMS!");

  let message = event.message;
  ok(message instanceof MozSmsMessage);

  is(message.delivery, "received");
  is(message.deliveryStatus, "success");
  is(message.sender, SENDER);
  is(message.receiver, RECEIVER);
  is(message.body, body);
  is(message.messageClass, "normal");
  ok(message.timestamp instanceof Date);
  // SMSC timestamp is in seconds.
  ok(Math.floor(message.timestamp.getTime() / 1000) >= Math.floor(now / 1000));

  cleanUp();
};

function cleanUp() {
  if (!completed) {
    window.setTimeout(cleanUp, 100);
    return;
  }

  SpecialPowers.removePermission("sms", document);
  finish();
}
