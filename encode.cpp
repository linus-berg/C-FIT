#include <math.h>
#include <cstdlib>
#include <fstream>
#include "csv.h"
#include "fit_date_time.hpp"
#include "fit_encode.hpp"
#include "fit_file_id_mesg.hpp"
#include "fit_mesg_broadcaster.hpp"

FIT_SINT32 DegreesToSemicircle(double degree) {
  return static_cast<FIT_SINT32>((degree * pow(2, 31)) / 180);
}

/* TODO: Idk make everything pretty? */
int ConvertCSVToFIT(std::string csv_file) {
  std::string raw_filename = csv_file.substr(0, csv_file.find_last_of("."));

  fit::Encode encode(fit::ProtocolVersion::V20);
  std::fstream fit_file;

  fit_file.open(raw_filename.append("_.fit"), std::ios::in | std::ios::out |
                                                  std::ios::binary |
                                                  std::ios::trunc);

  if (!fit_file.is_open()) {
    printf("Error opening FIT file.\n");
    return -1;
  }

  fit::FileIdMesg file_id_msg;
  file_id_msg.SetType(FIT_FILE_ACTIVITY);
  file_id_msg.SetManufacturer(FIT_MANUFACTURER_DYNASTREAM);
  file_id_msg.SetProduct(1337);
  file_id_msg.SetSerialNumber(1337);

  /* Write metadata. */
  encode.Open(fit_file);
  encode.Write(file_id_msg);

  io::CSVReader<11, io::trim_chars<' ', '\t', '"'>, io::no_quote_escape<','>,
                io::throw_on_overflow, io::single_and_empty_line_comment<'#'>>
      in(csv_file);

  in.read_header(io::ignore_extra_column,
                 /* Time */
                 "Time", "UTC Time",
                 /* Position */
                 "Latitude", "Longitude", "Altitude (m)",
                 /* Engine data. */
                 "Vehicle Speed (km/h) *OBD", "Engine Speed (RPM) *OBD",
                 "Throttle Position (%) *OBD", "Engine Coolant Temp (C) *OBD",
                 "Intake Air Temp (C) *OBD",
                 "Intake Manifold Pressure (kPa) *OBD");

  /* Time */
  double time;
  double utc;

  /* Position */
  double lat;
  double lon;
  double alt;

  /* Engine data */
  double speed;
  double rpm;
  double throttle_position;
  double engine_coolant_temp;
  double intake_air_temp;
  double intake_manifold_pressure;

  while (in.read_row(time, utc, lat, lon, alt, speed, rpm, throttle_position,
                     engine_coolant_temp, intake_air_temp,
                     intake_manifold_pressure)) {
    /* All OBDII messages for the FIT file. */
    std::list<fit::ObdiiDataMesg *> obd_messages;
    /* fit::ObdiiDataMesg speed_msg; */
    fit::ObdiiDataMesg rpm_msg;
    fit::ObdiiDataMesg throttle_msg;
    fit::ObdiiDataMesg engine_coolant_msg;
    fit::ObdiiDataMesg intake_air_msg;
    fit::ObdiiDataMesg intake_manifold_msg;

    /* obd_messages.push_back(&speed_msg); */
    obd_messages.push_back(&rpm_msg);
    obd_messages.push_back(&throttle_msg);
    obd_messages.push_back(&engine_coolant_msg);
    obd_messages.push_back(&intake_air_msg);
    obd_messages.push_back(&intake_manifold_msg);

    for (fit::ObdiiDataMesg *x : obd_messages) {
      /* OBDII records are #8. */
      x->SetLocalNum(8);
      x->SetTimestamp(static_cast<unsigned int>(time));
      x->SetTimestampMs(
          static_cast<FIT_UINT16>((time - static_cast<int>(time)) * 1000.0));
      x->SetSystemTime(0, static_cast<FIT_UINT32>(time * 1000.0));
    }

    /*
      OBD2 speed limit is 255, not enough for Cris. ¯\_(ツ)_/¯
      Instead we use the enhanced speed from global profiles.
    */
    /*
      speed_msg.SetPid(0x0D);
      speed_msg.SetRawData(0, speed);
    */

    /*
      Engine RPM is a bit special due to being a 2-byte value.
      As such OBDII calculates with (256A + B) / 4
    */
    rpm *= 4;

    rpm_msg.SetPid(0x0C);
    rpm_msg.SetSystemTime(1, static_cast<FIT_UINT32>(time * 1000.0));
    rpm_msg.SetRawData(0, static_cast<FIT_BYTE>(rpm >= 256 ? rpm / 256 : 0x00));
    rpm_msg.SetRawData(1, static_cast<FIT_BYTE>(rpm) % 256);

    throttle_msg.SetPid(0x11);
    throttle_msg.SetRawData(
        0, static_cast<FIT_BYTE>(throttle_position * (255 / 100.0)));

    engine_coolant_msg.SetPid(0x05);
    engine_coolant_msg.SetRawData(
        0, static_cast<FIT_BYTE>(engine_coolant_temp + 40));

    intake_air_msg.SetPid(0x0F);
    intake_air_msg.SetRawData(0, static_cast<FIT_BYTE>(intake_air_temp + 40));

    intake_manifold_msg.SetPid(0x0B);
    intake_manifold_msg.SetRawData(
        0, static_cast<FIT_BYTE>(intake_manifold_pressure));

    /* Write OBDII records to the fit file. */
    for (fit::ObdiiDataMesg *msg : obd_messages) {
      encode.Write(*msg);
    }

    fit::GpsMetadataMesg gps_msg;
    gps_msg.SetLocalNum(6);
    gps_msg.SetEnhancedSpeed(static_cast<FIT_FLOAT32>(speed / 3.6));
    gps_msg.SetTimestamp(static_cast<unsigned int>(time));
    gps_msg.SetTimestampMs(
        static_cast<FIT_UINT16>((time - static_cast<int>(time)) * 1000.0));
    gps_msg.SetPositionLat(DegreesToSemicircle(lat));
    gps_msg.SetPositionLong(DegreesToSemicircle(lon));
    /* Altitude in meters. */
    gps_msg.SetEnhancedAltitude(static_cast<FIT_FLOAT32>(alt));

    encode.Write(gps_msg);
  }

  if (!encode.Close()) {
    printf("Error closing encode.\n");
    return -1;
  }
  fit_file.close();

  return 0;
}

int main(int argc, const char *argv[]) {
  printf("Trancezor big BMW speed converter application\n");

  if (argc <= 1) {
    return 1;
  }
  return ConvertCSVToFIT(argv[1]);
}
