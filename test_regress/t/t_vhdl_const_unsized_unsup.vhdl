-- DESCRIPTION: Verilator: a constant whose element has no length has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is end entity;
architecture a of t is
  type string_array is array (natural range <>) of string;
  type rec is record
    data : std_logic_vector;
    dummy : integer;
  end record;
  type rec_array is array (natural range <>) of rec;
  constant strs : string_array(0 to 1) := ("TEST 0", "TEST 1");
  constant recs : rec_array := (("000", 0), ("00000", 1));
begin
end architecture;
