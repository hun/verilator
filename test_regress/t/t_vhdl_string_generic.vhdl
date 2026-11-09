-- DESCRIPTION: Verilator: VHDL string generics and their array attributes
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity seq_unit is
  generic (
    seq : string
  );
  port (
    idx   : in  integer range 0 to 7;
    lo_o  : out integer;
    hi_o  : out integer;
    len_o : out integer;
    bit_o : out std_logic
  );
end entity seq_unit;

architecture rtl of seq_unit is

  function to_bit (a : in character) return std_logic is
    variable ret : std_logic;
  begin
    case a is
      when '0' | '_' => ret := '0';
      when '1' | '-' => ret := '1';
      when others    => ret := 'X';
    end case;
    return ret;
  end function to_bit;

begin

  lo_o  <= seq'low;
  hi_o  <= seq'high;
  len_o <= seq'length;
  bit_o <= to_bit(seq(idx));

end architecture rtl;


library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    idx   : in  integer range 0 to 7;
    lo0   : out integer;
    hi0   : out integer;
    len0  : out integer;
    bit0  : out std_logic;
    lo1   : out integer;
    hi1   : out integer;
    len1  : out integer;
    bit1  : out std_logic
  );
end entity t;

architecture rtl of t is
begin

  -- Different lengths and contents; the specialization key must keep them apart.
  -- u0 uses a positional generic association, u1 a named one.
  u0 : entity work.seq_unit
    generic map ("0_1-")
    port map (idx => idx, lo_o => lo0, hi_o => hi0, len_o => len0, bit_o => bit0);

  u1 : entity work.seq_unit
    generic map (seq => "10_-0_")
    port map (idx => idx, lo_o => lo1, hi_o => hi1, len_o => len1, bit_o => bit1);

end architecture rtl;
