-- DESCRIPTION: Verilator: VHDL record types through GHDL XML
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee; use ieee.std_logic_1164.all;
package rp is
  type state_t is (idle, run);
  type inner_t is record
    hi : std_logic_vector(1 downto 0);
    lo : std_logic_vector(1 downto 0);
  end record;
  type rec_t is record
    en  : std_logic;
    st  : state_t;
    num : std_logic_vector(3 downto 0);
    inr : inner_t;
  end record;
  constant zero_rec : rec_t := (en => '0', st => idle, num => "0000",
                                inr => (hi => "00", lo => "00"));
end;
library ieee; use ieee.std_logic_1164.all; use work.rp.all;
entity t is
  port (clk : in std_logic; din : in rec_t; dout : out rec_t;
        y : out std_logic_vector(8 downto 0); st_o : out state_t;
        zero_o : out std_logic);
end;
architecture rtl of t is
  signal r : rec_t;
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      if din.en = '1' then
        r <= (din.en, din.st, din.num, (din.inr.lo, din.inr.hi));
      else
        r <= zero_rec;
      end if;
    end if;
  end process;

  dout <= r;
  st_o <= r.st;
  y <= r.num & r.inr.hi & r.inr.lo & r.en;
  zero_o <= '1' when r = zero_rec else '0';
end;
