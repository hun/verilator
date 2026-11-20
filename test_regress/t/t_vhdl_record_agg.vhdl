-- DESCRIPTION: Verilator: VHDL record aggregate with an others choice
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o : out std_logic_vector(7 downto 0);
    a : out std_logic;
    p : out std_logic_vector(7 downto 0);
    q : out std_logic;
    r : out std_logic_vector(7 downto 0);
    s : out std_logic
  );
end entity t;

architecture rtl of t is
  type rec_t is record
    dat   : std_logic_vector(7 downto 0);
    ack   : std_logic;
    stall : std_logic;
  end record;

  -- `others` fills the elements no name covers, one whole field at a time
  constant init_c : rec_t := (ack => '0', stall => '0', others => (others => '0'));
  constant ones_c : rec_t := (dat => "10101010", others => '1');
  -- A named element wins over the others value
  constant mix_c  : rec_t := (dat => x"3C", ack => '1', others => '0');

  signal s_init : rec_t := init_c;
begin
  o <= s_init.dat;
  a <= s_init.ack;
  p <= mix_c.dat;
  q <= mix_c.stall;
  r <= ones_c.dat;
  s <= ones_c.ack;
end architecture rtl;
