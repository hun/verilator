-- DESCRIPTION: Verilator: VHDL comparisons between a vector and an integer expression
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    iv     : in  integer;
    uva    : in  unsigned(3 downto 0);        -- "0101" = 5
    uvb    : in  unsigned(3 downto 0);        -- "1011" = 11 unsigned
    svb    : in  signed(3 downto 0);          -- "1011" = -5 signed
    o_uve  : out std_logic;                    -- uva  = iv
    o_uvn  : out std_logic;                    -- uva /= iv
    o_ivne : out std_logic;                    -- iv  /= uva  (integer on the left)
    o_ublt : out std_logic;                    -- uvb  < iv, unsigned
    o_sblt : out std_logic;                    -- svb  < iv, signed (same bits as uvb)
    o_sbeq : out std_logic;                    -- svb  = iv, signed
    o_ivgs : out std_logic                     -- iv  >= svb, signed
  );
end t;

architecture behav of t is
begin
  -- The integer is a dynamic port, not a constant: it is resized to the vector's type,
  -- signed for 'signed' and unsigned for 'unsigned', so uvb and svb disagree on '<'.
  o_uve  <= '1' when uva = iv  else '0';
  o_uvn  <= '1' when uva /= iv else '0';
  o_ivne <= '1' when iv /= uva else '0';
  o_ublt <= '1' when uvb < iv  else '0';
  o_sblt <= '1' when svb < iv  else '0';
  o_sbeq <= '1' when svb = iv  else '0';
  o_ivgs <= '1' when iv >= svb else '0';
end behav;
