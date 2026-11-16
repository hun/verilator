-- DESCRIPTION: Verilator: VHDL inlined function writing a slice of a local, called on a record element
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk : in  std_logic;
    rst : in  std_logic;
    o   : out std_logic_vector(4 downto 0)
  );
end entity t;

architecture rtl of t is
  subtype row_t is unsigned(4 downto 0);
  -- The second element puts the first one at an offset inside the record
  type rec_t is record
    row   : row_t;
    valid : std_logic;
  end record;
  signal r : rec_t;

  function next_row(row : row_t) return row_t is
    variable v   : std_ulogic_vector(4 downto 0);
    variable idx : unsigned(2 downto 0);
  begin
    v := std_ulogic_vector(row);
    idx := row(2 downto 0);
    -- A slice write into a local, whose offset must be taken from the local and not
    -- from the selects that binding the actual brought along
    v(2 downto 0) := std_ulogic_vector(idx + 1);
    return unsigned(v);
  end;
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        r.row <= (others => '0');
        r.valid <= '0';
      else
        r.valid <= '1';
        r.row <= next_row(r.row);
      end if;
    end if;
  end process;
  o <= std_logic_vector(r.row);
end architecture rtl;
