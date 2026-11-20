-- DESCRIPTION: Verilator: VHDL concurrent labels are unique
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  p : process begin y <= 1; wait; end process;
  p : process begin y <= 2; wait; end process;
end;
