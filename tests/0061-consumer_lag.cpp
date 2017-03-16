/*
 * librdkafka - Apache Kafka C library
 *
 * Copyright (c) 2016, Magnus Edenhill
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <iostream>
#include "testcpp.h"

/**
 * Verify consumer_lag
 */

static std::string topic;

class StatsCb : public RdKafka::EventCb {
 public:
  int64_t calc_lag; //calculated lag

  StatsCb() {
    calc_lag = -1;
  }

  void event_cb (RdKafka::Event &event) {
    if (event.type() == RdKafka::Event::EVENT_LOG) {
      Test::Say(tostr() << "LOG-" << event.severity() << "-" << event.fac() <<
                ": " << event.str() << "\n");
      return;
    } else if (event.type() != RdKafka::Event::EVENT_STATS)
      return;

    int64_t consumer_lag = parse_json(event.str().c_str());

    Test::Say(3, tostr() << "Stats: consumer_lag is " << consumer_lag << "\n");
    if (consumer_lag != calc_lag)
      Test::Fail(tostr() << "Stats consumer_lag " << consumer_lag << ", expected " << calc_lag << "\n");
  }

  /* Naiive JSON parsing, find the consumer_lag for partition 0
   * and return it. */
  static int64_t parse_json (const char *json_doc) {
    const std::string match_topic(std::string("\"") + topic + "\":");
    const char *search[] = { "\"topic\":",
                             match_topic.c_str(),
                             "\"0\":",
                             "\"consumer_lag\":",
                             NULL };
    const char *remain = json_doc;

    for (const char **sp = search ; *sp ; sp++) {
      const char *t = strstr(remain, *sp);
      if (!t)
        Test::Fail(tostr() << "Couldnt find " << *sp <<
                   " in remaining stats output:\n" << remain);
      remain = t + strlen(*sp);
    }

    while (*remain == ' ')
      remain++;

    if (!*remain)
      Test::Fail("Nothing following consumer_lag");

    return strtoull(remain, NULL, 0);
  }
};


static void do_test_consumer_lag (void) {
  const int msgcnt = 10;
  std::string errstr;
  RdKafka::ErrorCode err;

  topic = Test::mk_topic_name("0061-consumer_lag", 1);

  test_produce_msgs_easy(topic.c_str(), 0, 0, msgcnt);

  /*
   * Create consumer
   */

  /* Create consumer */
  RdKafka::Conf *conf;
  Test::conf_init(&conf, NULL, 10);
  StatsCb stats;
  if (conf->set("event_cb", &stats, errstr) != RdKafka::Conf::CONF_OK)
    Test::Fail("set event_cb failed: " + errstr);
  Test::conf_set(conf, "group.id", topic);
  Test::conf_set(conf, "enable.auto.commit", "false");
  Test::conf_set(conf, "enable.partition.eof", "false");
  Test::conf_set(conf, "auto.offset.reset", "earliest");
  Test::conf_set(conf, "statistics.interval.ms", "100");

  RdKafka::KafkaConsumer *c = RdKafka::KafkaConsumer::create(conf, errstr);
  if (!c)
    Test::Fail("Failed to create KafkaConsumer: " + errstr);
  delete conf;

  /* Assign partitions */
  /* Subscribe */
  std::vector<RdKafka::TopicPartition*> parts;
  parts.push_back(RdKafka::TopicPartition::create(topic, 0));
  if ((err = c->assign(parts)))
    Test::Fail("assign failed: " + RdKafka::err2str(err));

  /* Start consuming */
  Test::Say("Consuming topic " + topic + "\n");
  int cnt = 0;
  bool run = true;
  while (cnt < msgcnt) {
    RdKafka::Message *msg = c->consume(1000);
    switch (msg->err())
      {
      case RdKafka::ERR__TIMED_OUT:
        continue;
      case RdKafka::ERR__PARTITION_EOF:
        Test::Fail(tostr() << "Consume error after " << cnt << "/" << msgcnt << " messages: " << msg->errstr());
        run = false;
        continue;

      case RdKafka::ERR_NO_ERROR:
        /* Proper message. Updated calculated lag for later
         * checking in stats callback */
        stats.calc_lag = msgcnt - (msg->offset()+1);
        cnt++;
        Test::Say(2, tostr() << "Received message #" << cnt << "/" << msgcnt <<
                  " at offset " << msg->offset() << " (calc lag " << stats.calc_lag << ")\n");
        /* Slow down message "processing" to make sure we get
         * at least one stats callback per message. */
        if (cnt < msgcnt)
          rd_sleep(1);
        break;

      default:
        Test::Fail("Consume error: " + msg->errstr());
        break;
      }
  }
  Test::Say("Done\n");

  c->close();
  delete c;
}

extern "C" {
  int main_0061_consumer_lag (int argc, char **argv) {
    do_test_consumer_lag();
    return 0;
  }
}