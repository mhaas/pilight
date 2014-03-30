/*
	Copyright (C) 2014 CurlyMo

	This file is part of pilight.

    pilight is free software: you can redistribute it and/or modify it under the 
	terms of the GNU General Public License as published by the Free Software 
	Foundation, either version 3 of the License, or (at your option) any later 
	version.

    pilight is distributed in the hope that it will be useful, but WITHOUT ANY 
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR 
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "../../pilight.h"
#include "common.h"
#include "datetime.h"
#include "log.h"
#include "threads.h"
#include "http_lib.h"
#include "protocol.h"
#include "hardware.h"
#include "binary.h"
#include "json.h"
#include "gc.h"
#include "wunderground.h"

typedef struct wunderground_data_t {
	char *api;
	char *country;
	char *location;
	struct wunderground_data_t *next;
} wunderground_data_t;

unsigned short wunderground_loop = 1;
unsigned short wunderground_threads = 0;

void *wundergroundParse(void *param) {
	struct protocol_threads_t *thread = (struct protocol_threads_t *)param;
	struct JsonNode *json = (struct JsonNode *)thread->param;
	struct JsonNode *jid = NULL;
	struct JsonNode *jchild = NULL;
	struct JsonNode *jchild1 = NULL;
	struct JsonNode *node = NULL;
	struct wunderground_data_t *wunderground_data = NULL;
	struct wunderground_data_t *wtmp = NULL;	
	int interval = 900, nrloops = 0, ointerval = 900;
	
	char url[1024];
	char *filename = NULL, *data = NULL;
	char typebuf[70];
	char *stmp = NULL;
	double temp = 0;
	int humi = 0, lg = 0, ret = 0;

	JsonNode *jdata = NULL;
	JsonNode *jdata1 = NULL;
	JsonNode *jobs = NULL;
	JsonNode *jsun = NULL;
	JsonNode *jsunr = NULL;
	JsonNode *jsuns = NULL;
	char *shour = NULL, *smin = NULL;
	char *rhour = NULL, *rmin = NULL;

	time_t timenow = 0;

	wunderground_threads++;	
	
	int has_country = 0, has_api = 0, has_location = 0;
	if((jid = json_find_member(json, "id"))) {
		jchild = json_first_child(jid);
		while(jchild) {
			has_country = 0, has_api = 0, has_location = 0;
			jchild1 = json_first_child(jchild);
			struct wunderground_data_t* wnode = malloc(sizeof(struct wunderground_data_t));
			if(!wnode) {
				logprintf(LOG_ERR, "out of memory");
				exit(EXIT_FAILURE);
			}
			while(jchild1) {
				if(strcmp(jchild1->key, "api") == 0) {
					has_api = 1;
					wnode->api = malloc(strlen(jchild1->string_)+1);
					if(!wnode->api) {
						logprintf(LOG_ERR, "out of memory");
						exit(EXIT_FAILURE);
					}
					strcpy(wnode->api, jchild1->string_);
				}
				if(strcmp(jchild1->key, "location") == 0) {
					has_location = 1;
					wnode->location = malloc(strlen(jchild1->string_)+1);
					if(!wnode->location) {
						logprintf(LOG_ERR, "out of memory");
						exit(EXIT_FAILURE);
					}
					strcpy(wnode->location, jchild1->string_);
				}
				if(strcmp(jchild1->key, "country") == 0) {
					has_country = 1;
					wnode->country = malloc(strlen(jchild1->string_)+1);
					if(!wnode->country) {
						logprintf(LOG_ERR, "out of memory");
						exit(EXIT_FAILURE);
					}
					strcpy(wnode->country, jchild1->string_);
				}
				jchild1 = jchild1->next;
			}
			if(has_country && has_api && has_location) {
				wnode->next = wunderground_data;
				wunderground_data = wnode;
			} else {
				sfree((void *)&wnode);
			}
			jchild = jchild->next;
		}
	}

	json_find_number(json, "poll-interval", &interval);
	ointerval = interval;

	while(wunderground_loop) {
		wtmp = wunderground_data;
		if(protocol_thread_wait(thread, interval, &nrloops) == ETIMEDOUT) {
			interval = ointerval;
			while(wtmp) {
				filename = NULL;
				data = NULL;
				sprintf(url, "http://api.wunderground.com/api/%s/geolookup/conditions/q/%s/%s.json", wtmp->api, wtmp->country, wtmp->location);	
				http_parse_url(url, &filename);
				ret = http_get(filename, &data, &lg, typebuf);

				if(ret == 200) {
					if(strcmp(typebuf, "application/json;") == 0) {
						if(json_validate(data) == true) {
							if((jdata = json_decode(data)) != NULL) {
								if((jobs = json_find_member(jdata, "current_observation")) != NULL) {
									if((node = json_find_member(jobs, "temp_c")) == NULL) {
										printf("api.wunderground.com json has no temp_c key");
									} else if(json_find_string(jobs, "relative_humidity", &stmp) != 0) {
										printf("api.wunderground.com json has no relative_humidity key");
									} else {
										if(node->tag != JSON_NUMBER) {
											printf("api.wunderground.com json has no temp_c key");
										} else {
											if(data) {
												sfree((void *)&data);
												data = NULL;
											}
											if(filename) {
												sfree((void *)&filename);
												filename = NULL;
											}

											sprintf(url, "http://api.wunderground.com/api/%s/astronomy/q/%s/%s.json", wtmp->api, wtmp->country, wtmp->location);	
											http_parse_url(url, &filename);
											ret = http_get(filename, &data, &lg, typebuf);										
											if(ret == 200) {
												if(strcmp(typebuf, "application/json;") == 0) {
													if(json_validate(data) == true) {
														if((jdata1 = json_decode(data)) != NULL) {
															if((jsun = json_find_member(jdata1, "sun_phase")) != NULL) {
																if((jsunr = json_find_member(jsun, "sunrise")) != NULL
																   && (jsuns = json_find_member(jsun, "sunset")) != NULL) {
																	if(json_find_string(jsuns, "hour", &shour) != 0) {
																		printf("api.wunderground.com json has no sunset hour key");
																	} else if(json_find_string(jsuns, "minute", &smin) != 0) {
																		printf("api.wunderground.com json has no sunset minute key");
																	} else if(json_find_string(jsunr, "hour", &rhour) != 0) {
																		printf("api.wunderground.com json has no sunrise hour key");
																	} else if(json_find_string(jsunr, "minute", &rmin) != 0) {
																		printf("api.wunderground.com json has no sunrise minute key");
																	} else {
																		temp = node->number_;
																		sscanf(stmp, "%d%%", &humi);

																		timenow = time(NULL);
																		struct tm *current = localtime(&timenow);
																		int month = current->tm_mon+1;
																		int mday = current->tm_mday;
																		int year = current->tm_year+1900;
																		
																		time_t midnight = (datetime2ts(year, month, mday, 23, 59, 59, 0)+1);
																		time_t sunset = 0;
																		time_t sunrise = 0;

																		wunderground->message = json_mkobject();
																		
																		JsonNode *code = json_mkobject();
																		
																		json_append_member(code, "api", json_mkstring(wtmp->api));
																		json_append_member(code, "location", json_mkstring(wtmp->location));
																		json_append_member(code, "country", json_mkstring(wtmp->country));
																		json_append_member(code, "temperature", json_mknumber((int)(temp*100)));
																		json_append_member(code, "humidity", json_mknumber((int)(humi*100)));
																		sunrise = datetime2ts(year, month, mday, atoi(rhour), atoi(rmin), 0, 0);
																		json_append_member(code, "sunrise", json_mknumber((atoi(rhour)*100)+atoi(rmin)));
																		sunset = datetime2ts(year, month, mday, atoi(shour), atoi(smin), 0, 0);
																		json_append_member(code, "sunset", json_mknumber((atoi(shour)*100)+atoi(smin)));
																		if(timenow > sunrise && timenow < sunset) {
																			json_append_member(code, "sun", json_mkstring("rise"));
																		} else {
																			json_append_member(code, "sun", json_mkstring("set"));
																		}

																		json_append_member(wunderground->message, "message", code);
																		json_append_member(wunderground->message, "origin", json_mkstring("receiver"));
																		json_append_member(wunderground->message, "protocol", json_mkstring(wunderground->id));
																		
																		pilight.broadcast(wunderground->id, wunderground->message);
																		json_delete(wunderground->message);
																		wunderground->message = NULL;
																		/* Send message when sun rises */
																		if(sunrise > timenow) {
																			if((sunrise-timenow) < ointerval) {
																				interval = (int)(sunrise-timenow);
																			}
																		/* Send message when sun sets */
																		} else if(sunset > timenow) {
																			if((sunset-timenow) < ointerval) {
																				interval = (int)(sunset-timenow);
																			}
																		/* Update all values when a new day arrives */
																		} else {
																			if((midnight-timenow) < ointerval) {
																				interval = (int)(midnight-timenow);
																			}
																		}
																	}
																} else {
																	logprintf(LOG_NOTICE, "api.wunderground.com json has no sunset and/or sunrise key");
																}
															} else {
																logprintf(LOG_NOTICE, "api.wunderground.com json has no sun_phase key");
															}
															json_delete(jdata1);
														} else {
															logprintf(LOG_NOTICE, "api.wunderground.com json could not be parsed");
														}
													}  else {
														logprintf(LOG_NOTICE, "api.wunderground.com response was not in a valid json format");
													}
												} else {
														logprintf(LOG_NOTICE, "api.wunderground.com response was not in a valid json format");
												}
											} else {
												logprintf(LOG_NOTICE, "could not reach api.wundergrond.com");
											}
										}
									}
								} else {
									logprintf(LOG_NOTICE, "api.wunderground.com json has no current_observation key");
								}
								json_delete(jdata);
							} else {
								logprintf(LOG_NOTICE, "api.wunderground.com json could not be parsed");
							}
						} else {
							logprintf(LOG_NOTICE, "api.wunderground.com response was not in a valid json format");
						}
					} else {
						logprintf(LOG_NOTICE, "api.wunderground.com response was not in a valid json format");
					}
				} else {
					logprintf(LOG_NOTICE, "could not reach api.wundergrond.com");
				}
				if(data) {
					sfree((void *)&data);
				}
				if(filename) {
					sfree((void *)&filename);
				}
			wtmp = wtmp->next;
			}
		}			
	}

	while(wunderground_data) {
		wtmp = wunderground_data;
		sfree((void *)&wunderground_data->country);
		sfree((void *)&wunderground_data->location);
		wunderground_data = wunderground_data->next;
		sfree((void *)&wtmp);
	}
	sfree((void *)&wunderground_data);
	wunderground_threads--;
	return (void *)NULL;
}

struct threadqueue_t *wundergroundInitDev(JsonNode *jdevice) {
	wunderground_loop = 1;
	char *output = json_stringify(jdevice, NULL);
	JsonNode *json = json_decode(output);
	sfree((void *)&output);

	struct protocol_threads_t *node = protocol_thread_init(wunderground, json);
	return threads_register("wunderground", &wundergroundParse, (void *)node, 0);
}

int wundergroundCheckValues(JsonNode *code) {
	int interval = 900;

	json_find_number(code, "poll-interval", &interval);

	if(interval < 900) {
		return 1;
	}

	return 0;
}

void wundergroundThreadGC(void) {
	wunderground_loop = 0;
	protocol_thread_stop(wunderground);
	while(wunderground_threads > 0) {
		usleep(10);
	}
	protocol_thread_free(wunderground);
}

void wundergroundInit(void) {

	protocol_register(&wunderground);
	protocol_set_id(wunderground, "wunderground");
	protocol_device_add(wunderground, "wunderground", "Weather Underground API");
	wunderground->devtype = WEATHER;
	wunderground->hwtype = API;

	options_add(&wunderground->options, 't', "temperature", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,5}$");
	options_add(&wunderground->options, 'h', "humidity", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,5}$");
	options_add(&wunderground->options, 'a', "api", OPTION_HAS_VALUE, CONFIG_ID, JSON_STRING, NULL, "^[a-z0-9]+$");
	options_add(&wunderground->options, 'l', "location", OPTION_HAS_VALUE, CONFIG_ID, JSON_STRING, NULL, "^[a-z]+$");
	options_add(&wunderground->options, 'c', "country", OPTION_HAS_VALUE, CONFIG_ID, JSON_STRING, NULL, "^[a-z]+$");
	options_add(&wunderground->options, 'u', "sunrise", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]{3,4}$");
	options_add(&wunderground->options, 'd', "sunset", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]{3,4}$");
	options_add(&wunderground->options, 's', "sun", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_STRING, NULL, NULL);	

	options_add(&wunderground->options, 0, "device-decimals", OPTION_HAS_VALUE, CONFIG_SETTING, JSON_NUMBER, (void *)2, "[0-9]");
	options_add(&wunderground->options, 0, "gui-decimals", OPTION_HAS_VALUE, CONFIG_SETTING, JSON_NUMBER, (void *)2, "[0-9]");
	options_add(&wunderground->options, 0, "gui-show-humidity", OPTION_HAS_VALUE, CONFIG_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
	options_add(&wunderground->options, 0, "gui-show-temperature", OPTION_HAS_VALUE, CONFIG_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
	options_add(&wunderground->options, 0, "gui-show-sunriseset", OPTION_HAS_VALUE, CONFIG_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
	options_add(&wunderground->options, 0, "poll-interval", OPTION_HAS_VALUE, CONFIG_SETTING, JSON_NUMBER, (void *)900, "[0-9]");

	wunderground->initDev=&wundergroundInitDev;
	wunderground->checkValues=&wundergroundCheckValues;
	wunderground->threadGC=&wundergroundThreadGC;
}