-- DESCRIPTION: Verilator: VHDL nested real and physical objects are refused
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- An array element or record member has no packed run-time model when it is
-- real or physical, even though the enclosing object does.

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  type real_array_t is array (0 to 3) of real;
  type nested_array_t is array (0 to 1) of real_array_t;
  type real_record_t is record
    f : real;
  end record;
  type time_array_t is array (0 to 3) of time;
  signal r : real_array_t;
  signal rn : nested_array_t;
  signal rr : real_record_t;
  signal tv : time_array_t;
begin
  -- A conversion and a relation both need the element's data type
  y <= integer(r(0));
  y <= integer(rn(0)(1));
  y <= integer(rr.f);
  y <= integer(tv(0));
  y <= 1 when r(0) < r(1) else 0;
end;
