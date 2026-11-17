-- DESCRIPTION: Verilator: VHDL report severity failure stops the run
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (done : out bit);
end;

architecture rtl of t is
begin
  process
  begin
    report "stopping now" severity failure;
    done <= '1';
    wait;
  end process;
end;
